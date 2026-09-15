#include "stone.h"

#include "../app.h"
#include "../core.h"
#include "../ui/widgets.h"
#include "../util/log.h"
#include "../util/string_conv.h"
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

#include <psapi.h>
#include <tlhelp32.h>

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

std::string ToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string ModuleName(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH))
        return "<unknown>";
    return std::filesystem::path(Narrow(path)).filename().string();
}

// Patching ntdll, kernelbase or any other dll that ships with Windows is a
// one way ticket to a Unity crash dialog, so those are never scanned.
bool IsSystemModule(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH))
        return true;

    const std::string lower = ToLower(Narrow(path));

    wchar_t buffer[MAX_PATH]{};
    if (GetWindowsDirectoryW(buffer, MAX_PATH) && lower.find(ToLower(Narrow(buffer))) == 0)
        return true;
    if (GetSystemDirectoryW(buffer, MAX_PATH) && lower.find(ToLower(Narrow(buffer))) == 0)
        return true;

    // Known non-game runtimes that can legitimately contain the same bytes.
    static const char* const runtime[] = {
        "ntdll.dll", "kernel32.dll", "kernelbase.dll", "user32.dll", "gdi32.dll",
        "d3d11.dll", "dxgi.dll", "msvcp140.dll", "vcruntime140.dll", "ucrtbase.dll",
    };
    const std::string name = ToLower(ModuleName(module));
    for (const char* entry : runtime) {
        if (name == entry)
            return true;
    }
    return false;
}

// A pattern byte of -1 matches anything.
bool MatchesHere(const std::uint8_t* data, const std::vector<int>& pattern)
{
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] < 0)
            continue;
        if (data[i] != static_cast<std::uint8_t>(pattern[i]))
            return false;
    }
    return true;
}

// Scans the committed, executable pages of one module.
std::uint8_t* ScanModule(void* base, std::size_t size, const std::vector<int>& pattern)
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

        if (committed && executable && memory.RegionSize >= pattern.size()) {
            auto* begin = static_cast<std::uint8_t*>(memory.BaseAddress);
            const std::size_t regionSize = memory.RegionSize;
            for (std::size_t offset = 0; offset + pattern.size() <= regionSize; ++offset) {
                if (MatchesHere(begin + offset, pattern))
                    return begin + offset;
            }
        }

        region = static_cast<std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
    }
    return nullptr;
}

// Writes what the scan sees into the log, once per session.
void LogModuleDump()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        log::Error("stone: EnumProcessModules failed (%lu)", GetLastError());
        return;
    }

    const DWORD count = needed / sizeof(HMODULE);
    log::Info("stone: %lu modules loaded, scanning for 41 89 87 78 05 00 00",
              static_cast<unsigned long>(count));

    for (DWORD i = 0; i < count && i < 1024 && i < 40; ++i) {
        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)))
            continue;
        log::Info("stone:   %s%s (base 0x%p, %lu KB)", ModuleName(modules[i]).c_str(),
                  IsSystemModule(modules[i]) ? " [skipped: system]" : "",
                  info.lpBaseOfDll, static_cast<unsigned long>(info.SizeOfImage / 1024));
    }
}

std::vector<Hit> FindPattern(const std::vector<int>& pattern)
{
    std::vector<Hit> hits;

    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return hits;

    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count && i < 1024; ++i) {
        if (modules[i] == bd::g_module)   // never hook our own dll
            continue;
        if (IsSystemModule(modules[i]))   // never hook a Windows dll
            continue;

        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)))
            continue;
        if (!info.lpBaseOfDll || info.SizeOfImage < pattern.size())
            continue;

        if (std::uint8_t* found = ScanModule(info.lpBaseOfDll, info.SizeOfImage, pattern))
            hits.push_back({ modules[i], found });
    }
    return hits;
}

// Overwriting seven bytes while another thread is sitting in the middle of them
// is how a hook turns into a crash, so everybody else is parked for the write.
class ThreadFreeze {
public:
    ThreadFreeze() { Suspend(); }
    ~ThreadFreeze() { Resume(); }

    // True when some thread is executing inside [address, address + size).
    bool IsBusy(const void* address, std::size_t size) const
    {
        const auto begin = reinterpret_cast<std::uintptr_t>(address);
        const auto end = begin + size;

        for (HANDLE thread : threads_) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread, &context))
                continue;
#ifdef _WIN64
            if (context.Rip >= begin && context.Rip < end)
                return true;
#else
            if (context.Eip >= begin && context.Eip < end)
                return true;
#endif
        }
        return false;
    }

private:
    void Suspend()
    {
        const DWORD process = GetCurrentProcessId();
        const DWORD self = GetCurrentThreadId();

        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return;

        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry)) {
            do {
                if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self)
                    continue;

                HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                               THREAD_QUERY_INFORMATION,
                                           FALSE, entry.th32ThreadID);
                if (!thread)
                    continue;
                if (SuspendThread(thread) == static_cast<DWORD>(-1))
                    CloseHandle(thread);
                else
                    threads_.push_back(thread);
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    void Resume()
    {
        for (HANDLE thread : threads_) {
            ResumeThread(thread);
            CloseHandle(thread);
        }
        threads_.clear();
    }

    std::vector<HANDLE> threads_;
};

// A 5 byte relative jump only reaches +/- 2 GB, so the stub has to be allocated
// close to the instruction we are patching.
void* AllocateNear(const void* target, std::size_t size)
{
    if (!target)
        return nullptr;

    const auto anchor =
        reinterpret_cast<std::uintptr_t>(target) & ~static_cast<std::uintptr_t>(0xFFFF);

    for (std::int64_t distance = kAllocationStep; distance < kMaxJumpDistance;
         distance += kAllocationStep) {
        for (int direction = 1; direction >= -1; direction -= 2) {
            auto* wanted =
                reinterpret_cast<void*>(anchor + static_cast<std::uintptr_t>(direction * distance));
            if (void* memory = VirtualAlloc(wanted, size, MEM_COMMIT | MEM_RESERVE,
                                            PAGE_EXECUTE_READWRITE))
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
    // Stays on when the hook fails: the menu then shows why, and OnFrame keeps
    // retrying in case the game has not loaded the code yet.
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
    const std::vector<int> exact(kPatternBytes, kPatternBytes + kPatchSize);
    std::vector<Hit> hits = FindPattern(exact);
    bool loose = false;

    if (hits.empty() && looseMatch_) {
        // Any "mov [r15+disp32], eax": the stub replays the original bytes, so a
        // moved counter still works - but it can also hit the wrong value.
        hits = FindPattern({ 0x41, 0x89, 0x87, -1, -1, 0x00, 0x00 });
        loose = !hits.empty();
    }

    if (hits.empty()) {
        status_ = "pattern not found (retrying)";
        log::Error("stone: instruction not found in any game module");
        LogModuleDump();
        return false;
    }

    // Prefer the game exe, otherwise the first non-system module.
    std::size_t index = 0;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        log::Info("stone: match in %s at 0x%p", ModuleName(hits[i].module).c_str(),
                  static_cast<void*>(hits[i].address));
        if (hits[i].module == GetModuleHandleW(nullptr))
            index = i;
    }

    std::uint8_t* found = hits[index].address;
    module_ = hits[index].module;
    matchCount_ = static_cast<int>(hits.size());

    if (loose)
        log::Warning("stone: using a loose match, check that it is really the stone counter");

    void* memory = AllocateNear(found, kStubSize);
    if (!memory) {
        status_ = "could not allocate memory near the game code";
        log::Error("stone: no memory within 2 GB of the instruction");
        LogModuleDump();
        target_ = nullptr;
        return false;
    }

    auto* code = static_cast<std::uint8_t*>(memory);
    std::size_t at = 0;

    code[at++] = 0x50; // push rax
    if (mode_ == Mode::Add) {
        code[at++] = 0x8D; // lea eax, [rax + amount] (does not touch the flags)
        code[at++] = 0x80;
    } else {
        code[at++] = 0xB8; // mov eax, amount
    }
    const std::int32_t amount = static_cast<std::int32_t>(amount_);
    std::memcpy(code + at, &amount, sizeof(amount));
    at += sizeof(amount);

    std::memcpy(code + at, found, kPatchSize); // the original mov [r15+...], eax
    at += kPatchSize;

    code[at++] = 0x58; // pop rax
    code[at++] = 0xFF; // jmp qword ptr [rip+0]
    code[at++] = 0x25;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;

    const std::uint64_t back = reinterpret_cast<std::uint64_t>(found + kPatchSize);
    std::memcpy(code + at, &back, sizeof(back));
    at += sizeof(back);

    FlushInstructionCache(GetCurrentProcess(), code, at);

    const std::int64_t relative =
        reinterpret_cast<std::int64_t>(code) - (reinterpret_cast<std::int64_t>(found) + 5);
    if (relative < INT32_MIN || relative > INT32_MAX) {
        status_ = "stub is out of jump range";
        VirtualFree(memory, 0, MEM_RELEASE);
        return false;
    }

    // Write the diversion with every other thread parked, and back off if one of
    // them is standing in the bytes we are about to overwrite.
    std::uint8_t patch[kPatchSize];
    const std::int32_t jump = static_cast<std::int32_t>(relative);
    patch[0] = 0xE9;
    std::memcpy(patch + 1, &jump, sizeof(jump));
    patch[5] = 0x90;
    patch[6] = 0x90;

    {
        ThreadFreeze freeze;
        if (freeze.IsBusy(found, kPatchSize)) {
            status_ = "waiting for a safe moment";
            VirtualFree(memory, 0, MEM_RELEASE);
            return false;
        }

        std::memcpy(original_, found, kPatchSize);
        target_ = found;
        stub_ = memory;
        stubSize_ = kStubSize;

        DWORD protection = 0;
        if (!VirtualProtect(found, kPatchSize, PAGE_EXECUTE_READWRITE, &protection)) {
            status_ = "VirtualProtect failed";
            log::Error("stone: VirtualProtect failed (%lu)", GetLastError());
            target_ = nullptr;
            stub_ = nullptr;
            VirtualFree(memory, 0, MEM_RELEASE);
            return false;
        }

        std::memcpy(found, patch, kPatchSize);
        VirtualProtect(found, kPatchSize, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), found, kPatchSize);
    }

    installed_ = true;

    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "hooked in %s at +0x%llX (%d match%s)",
                  ModuleName(module_).c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(found) -
                                                  reinterpret_cast<std::uintptr_t>(module_)),
                  matchCount_, matchCount_ == 1 ? "" : "es");
    status_ = buffer;
    log::Info("stone: hooked %s at 0x%p (mode %s, amount %d)", ModuleName(module_).c_str(),
              static_cast<void*>(found), mode_ == Mode::Add ? "add" : "set", amount_);
    return true;
#endif
}

void StoneModule::Remove()
{
    if (!installed_ || !target_)
        return;

    ThreadFreeze freeze;

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

    ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.45f, 1.0f), "%s", status_.c_str());
    ImGui::TextDisabled("Only game modules are scanned - Windows dlls are skipped.");

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

    if (ui::Toggle("Loose match", &looseMatch_,
                   "Off: only the exact 41 89 87 78 05 00 00 bytes. On: any "
                   "mov [r15+disp32], eax, which can hook the wrong value.")) {
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
    out.set("looseMatch", json::Value(looseMatch_));
}

void StoneModule::OnLoad(const json::Value& in)
{
    if (const json::Value* v = in.find("mode"))
        mode_ = static_cast<Mode>(v->asInt(static_cast<int>(mode_)));
    if (const json::Value* v = in.find("amount"))
        amount_ = v->asInt(amount_);
    if (const json::Value* v = in.find("looseMatch"))
        looseMatch_ = v->asBool(looseMatch_);
}

} // namespace bd
