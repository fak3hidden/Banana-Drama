#include "stone.h"

#include "../app.h"
#include "../core.h"
#include "../ui/widgets.h"
#include "../util/log.h"
#include "../util/string_conv.h"
#include "imgui.h"

#include <cstring>
#include <filesystem>
#include <vector>

#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace bd {

namespace {

// mov [r15+0x578], eax - the instruction the stone counter is stored with.
constexpr std::uint8_t kPatternBytes[] = { 0x41, 0x89, 0x87, 0x78, 0x05, 0x00, 0x00 };
constexpr std::size_t kPatchSize = sizeof(kPatternBytes);
constexpr std::size_t kStubSize = 64;
constexpr std::int64_t kMaxJumpDistance = 0x7F000000; // just under 2 GB
constexpr std::int64_t kAllocationStep = 0x10000;     // 64 KB granularity

struct Hit {
    HMODULE module = nullptr;
    std::uint8_t* address = nullptr;
};

std::string ModuleName(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH))
        return "<unknown>";
    return std::filesystem::path(Narrow(path)).filename().string();
}

// Scans the committed, executable pages of one module. Reading the mapped image
// is what Cheat Engine does too, and skipping non-committed pages keeps it from
// touching gaps.
std::uint8_t* ScanModule(void* base, std::size_t size)
{
    auto* region = static_cast<std::uint8_t*>(base);
    auto* const end = region + size;

    while (region < end) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(region, &memory, sizeof(memory)))
            break;

        const bool committed = memory.State == MEM_COMMIT;
        const bool executable =
            (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY)) != 0;

        if (committed && executable && memory.RegionSize >= kPatchSize) {
            auto* begin = static_cast<std::uint8_t*>(memory.BaseAddress);
            const std::size_t size = memory.RegionSize;
            for (std::size_t offset = 0; offset + kPatchSize <= size; ++offset) {
                if (std::memcmp(begin + offset, kPatternBytes, kPatchSize) == 0)
                    return begin + offset;
            }
        }

        region = static_cast<std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
    }
    return nullptr;
}

// Every loaded module, not just the exe: with IL2CPP or Mono the game code lives
// in GameAssembly.dll or another dll, which is where the Cheat Engine aobscan
// finds this instruction.
std::vector<Hit> FindPattern(int* scannedModules)
{
    std::vector<Hit> hits;
    if (scannedModules)
        *scannedModules = 0;

    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return hits;

    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count && i < 1024; ++i) {
        if (modules[i] == bd::g_module) // never hook our own dll
            continue;

        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)))
            continue;
        if (!info.lpBaseOfDll || info.SizeOfImage < kPatchSize)
            continue;

        if (scannedModules)
            ++*scannedModules;
        if (std::uint8_t* found = ScanModule(info.lpBaseOfDll, info.SizeOfImage))
            hits.push_back({ modules[i], found });
    }
    return hits;
}

// A 5 byte relative jump only reaches +/- 2 GB, so the stub has to be allocated
// close to the instruction we are patching.
void* AllocateNear(const void* target, std::size_t size)
{
    if (!target)
        return nullptr;

    const auto anchor = reinterpret_cast<std::uintptr_t>(target) & ~static_cast<std::uintptr_t>(0xFFFF);

    for (std::int64_t distance = kAllocationStep; distance < kMaxJumpDistance; distance += kAllocationStep) {
        for (int direction = 1; direction >= -1; direction -= 2) {
            auto* wanted = reinterpret_cast<void*>(anchor + static_cast<std::uintptr_t>(direction * distance));
            if (void* memory = VirtualAlloc(wanted, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                return memory;
        }
    }
    return nullptr;
}

} // namespace

StoneModule::StoneModule()
    : Module("stone", "Stone",
             "Adds to the stone counter whenever the game stores it (hook on mov [r15+0x578], eax).")
{
}

StoneModule::~StoneModule()
{
    Remove();
}

void StoneModule::OnEnable()
{
    // Deliberately stays on when the hook fails: the menu then shows why, and
    // OnFrame keeps retrying in case the module is not loaded yet.
    if (!Install())
        log::Warning("stone: hook not installed yet, retrying every couple of seconds");
}

void StoneModule::OnDisable()
{
    Remove();
}

void StoneModule::OnFrame()
{
    ++frames_;

    // Retry roughly every two seconds until it sticks.
    if (!installed_ && frames_ % 120 == 0)
        Install();
}

bool StoneModule::Install()
{
    if (installed_)
        return true;

#ifndef _WIN64
    status_ = "64-bit only - build the x64 configuration";
    log::Error("stone: this hook needs the 64-bit build");
    return false;
#else
    int scanned = 0;
    const std::vector<Hit> hits = FindPattern(&scanned);
    if (hits.empty()) {
        status_ = "pattern not found (retrying)";
        log::Error("stone: instruction not found in %d loaded modules", scanned);
        return false;
    }

    std::size_t index = 0;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        log::Info("stone: match in %s", ModuleName(hits[i].module).c_str());
        if (hits[i].module == GetModuleHandleW(nullptr))
            index = i;
    }
    if (hits.size() > 1)
        log::Warning("stone: %zu matches, using the one in %s", hits.size(),
                     ModuleName(hits[index].module).c_str());

    std::uint8_t* found = hits[index].address;
    module_ = hits[index].module;

    std::memcpy(original_, found, kPatchSize);
    target_ = found;

    void* memory = AllocateNear(found, kStubSize);
    if (!memory) {
        status_ = "could not allocate memory near the game code";
        log::Error("stone: no memory within 2 GB of the instruction");
        target_ = nullptr;
        return false;
    }

    auto* code = static_cast<std::uint8_t*>(memory);
    std::size_t at = 0;

    code[at++] = 0x50; // push rax
    if (mode_ == Mode::Add) {
        code[at++] = 0x8D; // lea eax, [rax + amount] (no effect on the flags)
        code[at++] = 0x80;
    } else {
        code[at++] = 0xB8; // mov eax, amount
    }
    std::int32_t amount = static_cast<std::int32_t>(amount_);
    std::memcpy(code + at, &amount, sizeof(amount));
    at += sizeof(amount);

    std::memcpy(code + at, original_, kPatchSize); // the original mov [r15+...], eax
    at += kPatchSize;

    code[at++] = 0x58;             // pop rax
    code[at++] = 0xFF;             // jmp qword ptr [rip+0]
    code[at++] = 0x25;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;

    const std::uint64_t back = reinterpret_cast<std::uint64_t>(found + kPatchSize);
    std::memcpy(code + at, &back, sizeof(back));
    at += sizeof(back);

    FlushInstructionCache(GetCurrentProcess(), code, at);
    stub_ = memory;
    stubSize_ = kStubSize;

    // Divert the game's instruction: jmp rel32 followed by two nops.
    const std::int64_t relative =
        reinterpret_cast<std::int64_t>(code) - (reinterpret_cast<std::int64_t>(found) + 5);
    if (relative < INT32_MIN || relative > INT32_MAX) {
        status_ = "stub is out of jump range";
        VirtualFree(stub_, 0, MEM_RELEASE);
        stub_ = nullptr;
        target_ = nullptr;
        return false;
    }

    DWORD protection = 0;
    if (!VirtualProtect(found, kPatchSize, PAGE_EXECUTE_READWRITE, &protection)) {
        status_ = "VirtualProtect failed";
        log::Error("stone: VirtualProtect failed (%lu)", GetLastError());
        VirtualFree(stub_, 0, MEM_RELEASE);
        stub_ = nullptr;
        target_ = nullptr;
        return false;
    }

    const std::int32_t jump = static_cast<std::int32_t>(relative);
    found[0] = 0xE9;
    std::memcpy(found + 1, &jump, sizeof(jump));
    found[5] = 0x90;
    found[6] = 0x90;

    VirtualProtect(found, kPatchSize, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), found, kPatchSize);

    installed_ = true;

    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "hooked in %s at +0x%llX",
                  ModuleName(module_).c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(found) -
                                                  reinterpret_cast<std::uintptr_t>(module_)));
    status_ = buffer;
    log::Info("stone: hooked at 0x%p (mode %s, amount %d)", static_cast<void*>(found),
              mode_ == Mode::Add ? "add" : "set", amount_);
    return true;
#endif
}

void StoneModule::Remove()
{
    if (!installed_ || !target_)
        return;

    DWORD protection = 0;
    if (VirtualProtect(target_, kPatchSize, PAGE_EXECUTE_READWRITE, &protection)) {
        std::memcpy(target_, original_, kPatchSize);
        VirtualProtect(target_, kPatchSize, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), target_, kPatchSize);
        log::Info("stone: original bytes restored");
    } else {
        log::Error("stone: could not restore the original bytes (%lu)", GetLastError());
    }

    // The stub is deliberately not freed: a thread can still be sitting in it,
    // and 64 bytes is not worth crashing the game over.
    stub_ = nullptr;
    target_ = nullptr;
    installed_ = false;
    module_ = nullptr;
    status_ = "not installed";
}

void StoneModule::Reinstall()
{
    if (!installed_)
        return;
    Remove();
    Install();
}

void StoneModule::OnMenu()
{
    const bool live = installed_;
    if (!live) {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "%s - retrying", status_.c_str());
        ImGui::TextDisabled("Leave it on: it hooks as soon as the instruction shows up.");
        if (!ImGui::Button("Retry now"))
            return;
        Install();
        return;
    }

    ImGui::TextColored(live ? ImVec4(0.45f, 0.90f, 0.45f, 1.0f) : ImVec4(0.95f, 0.55f, 0.30f, 1.0f),
                       "%s", status_.c_str());

    int mode = static_cast<int>(mode_);
    const char* modes[] = { "Add on every gain", "Set to a fixed value" };
    if (ImGui::Combo("Mode", &mode, modes, 2)) {
        mode_ = static_cast<Mode>(mode);
        Reinstall();
        app::MarkSettingsDirty();
    }

    int amount = amount_;
    if (ImGui::InputInt("Amount", &amount, 1000, 100000)) {
        amount_ = amount < 0 ? 0 : amount;
        Reinstall();
        app::MarkSettingsDirty();
    }

    if (ImGui::Button("Re-hook")) {
        Reinstall();
        app::MarkSettingsDirty();
    }

    ImGui::TextDisabled("%d frames since it was switched on", frames_);
    ImGui::TextDisabled("Single-player / offline only.");
}

void StoneModule::OnSave(json::Value& out) const
{
    out.set("mode", json::Value(static_cast<int>(mode_)));
    out.set("amount", json::Value(amount_));
}

void StoneModule::OnLoad(const json::Value& in)
{
    if (const json::Value* v = in.find("mode"))
        mode_ = static_cast<Mode>(v->asInt(static_cast<int>(mode_)));
    if (const json::Value* v = in.find("amount"))
        amount_ = v->asInt(amount_);
}

} // namespace bd
