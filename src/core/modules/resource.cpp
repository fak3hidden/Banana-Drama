#include "resource.h"

#include "module_manager.h"

#include "../app.h"
#include "../ui/widgets.h"
#include "../util/log.h"
#include "../util/string_conv.h"
#include "imgui.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

#include <psapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "psapi.lib")

namespace bd {

namespace {

// mov [r15+0x578], eax - the instruction the stone counter is stored with.
// mov [r15+disp32], eax - the seven bytes that get replaced. A pattern can be
// longer than this: the extra bytes only make the scan unique, they are left
// alone.
constexpr std::size_t kInstructionSize = 7;

// "41 89 87 70 05 00 00", for the log.
std::string PatternText(const ResourceDef& def)
{
    std::string text;
    char buffer[8];
    for (std::size_t i = 0; i < def.patternSize; ++i) {
        std::snprintf(buffer, sizeof(buffer), "%02X ", def.pattern[i]);
        text += buffer;
    }
    if (!text.empty())
        text.pop_back();
    return text;
}

// Every message is tagged with the resource it came from (stone, wood, ...).
void Trace(const ResourceDef& def, log::Level level, const char* format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    const std::string line = std::string(def.id) + ": " + message;
    switch (level) {
    case log::Level::Debug:   log::Debug("%s", line.c_str()); break;
    case log::Level::Info:    log::Info("%s", line.c_str()); break;
    case log::Level::Warning: log::Warning("%s", line.c_str()); break;
    case log::Level::Error:   log::Error("%s", line.c_str()); break;
    }
}
constexpr std::size_t kStubSize = 64;
constexpr std::int64_t kMaxJumpDistance = 0x7F000000;
constexpr std::int64_t kAllocationStep = 0x10000;
constexpr std::size_t kMaxCandidates = 32;

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

// Patching a dll that ships with Windows is a one way ticket to a Unity crash
// dialog, so those are never scanned.
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

void ScanRange(std::uint8_t* begin, std::size_t size, const std::vector<int>& pattern,
               std::vector<std::uintptr_t>& out)
{
    for (std::size_t offset = 0; offset + pattern.size() <= size; ++offset) {
        if (MatchesHere(begin + offset, pattern))
            out.push_back(reinterpret_cast<std::uintptr_t>(begin + offset));
    }
}

// Reading is guarded by VirtualQuery, but a fault here would take the whole
// game with it, so on MSVC the scan sits inside structured exception handling.
bool SafeScanRange(const ResourceDef& def, std::uint8_t* begin, std::size_t size,
                   const std::vector<int>& pattern, std::vector<std::uintptr_t>& out)
{
#ifdef _MSC_VER
    __try {
        ScanRange(begin, size, pattern, out);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    ScanRange(begin, size, pattern, out);
    return true;
#endif
}

// Scans the committed, executable pages of one module.
void ScanModule(const ResourceDef& def, void* base, std::size_t size,
                const std::vector<int>& pattern, std::vector<std::uintptr_t>& out)
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
            if (!SafeScanRange(def, static_cast<std::uint8_t*>(memory.BaseAddress),
                               memory.RegionSize, pattern, out))
                Trace(def, log::Level::Warning, "could not read 0x%p, skipping that region",
                      memory.BaseAddress);
        }

        region = static_cast<std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
    }
}

struct ModuleRange {
    HMODULE module = nullptr;
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    bool system = false;
};

std::vector<ModuleRange> LoadedModules()
{
    std::vector<ModuleRange> ranges;

    HMODULE modules[1024]{};
    DWORD needed = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
        return ranges;

    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count && i < 1024; ++i) {
        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)))
            continue;

        ModuleRange range;
        range.module = modules[i];
        range.begin = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
        range.end = range.begin + info.SizeOfImage;
        range.system = IsSystemModule(modules[i]);
        ranges.push_back(range);
    }
    return ranges;
}

std::string LabelFor(std::uintptr_t address, const std::vector<ModuleRange>& modules)
{
    for (const ModuleRange& range : modules) {
        if (address >= range.begin && address < range.end)
            return ModuleName(range.module);
    }
    return "memory";
}

// Mono keeps its compiled code in allocated memory, so a match outside every
// module is described by the region it sits in.
HMODULE GetModuleByBase(const std::vector<ModuleRange>& modules, std::uintptr_t base)
{
    for (const ModuleRange& range : modules) {
        if (range.begin == base)
            return range.module;
    }
    return nullptr;
}

std::string JitLabel(std::uintptr_t address)
{
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)))
        return "memory";

    const auto base = reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "jit 0x%llX+0x%llX",
                  static_cast<unsigned long long>(base),
                  static_cast<unsigned long long>(address - base));
    return buffer;
}

std::uintptr_t BaseFor(std::uintptr_t address, const std::vector<ModuleRange>& modules)
{
    for (const ModuleRange& range : modules) {
        if (address >= range.begin && address < range.end)
            return range.begin;
    }
    return 0;
}

std::string Describe(std::uintptr_t address, const std::vector<ModuleRange>& modules)
{
    const std::uintptr_t base = BaseFor(address, modules);
    char buffer[128];
    if (base == 0)
        return JitLabel(address);

    std::snprintf(buffer, sizeof(buffer), "%s+0x%llX", ModuleName(GetModuleByBase(modules, base)).c_str(),
                  static_cast<unsigned long long>(address - base));
    return buffer;
}

// Writes what the scan sees into the log, once per session.
void LogModuleDump(const ResourceDef& def, const std::vector<ModuleRange>& modules)
{
    static bool done = false;
    if (done)
        return;
    done = true;

    Trace(def, log::Level::Info, "%zu modules loaded, looking for %s", modules.size(),
          PatternText(def).c_str());
    for (std::size_t i = 0; i < modules.size() && i < 40; ++i) {
        Trace(def, log::Level::Info, "  %s%s (base 0x%p, %llu KB)", ModuleName(modules[i].module).c_str(),
                  modules[i].system ? " [skipped: system]" : "",
                  reinterpret_cast<void*>(modules[i].begin),
                  static_cast<unsigned long long>((modules[i].end - modules[i].begin) / 1024));
    }
}

// Overwriting seven bytes while another thread sits in the middle of them is how
// a hook turns into a crash, so everybody else is parked for the write.
class ThreadFreeze {
public:
    ThreadFreeze() { Suspend(); }
    ~ThreadFreeze() { Resume(); }

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

ResourceModule::ResourceModule(const ResourceDef& def)
    : Module(def.id, def.label, Description(def)), def_(def)
{
}

std::string ResourceModule::Description(const ResourceDef& def)
{
    char buffer[192];
    std::snprintf(buffer, sizeof(buffer),
                  "Adds to the %s counter whenever the game stores it (hook on mov "
                  "[r15+0x%X], eax).",
                  def.label, static_cast<unsigned>(def.offset));
    return buffer;
}

ResourceModule::~ResourceModule()
{
    Remove();
}

void ResourceModule::OnEnable()
{
    // Nothing happens by itself: scanning and hooking are both explicit button
    // presses, so a crash can always be traced to the step that caused it.
    if (autoHook_ && !candidates_.empty()) {
        if (Hook(candidates_[static_cast<std::size_t>(selected_)].address))
            return;
        Trace(def_, log::Level::Warning, "auto-hook failed, pick a match by hand");
    }

    if (!installed_)
        status_ = "press Scan to look for the instruction";
}

void ResourceModule::OnDisable()
{
    Remove();
}

void ResourceModule::OnFrame()
{
    ++frames_;
}

void ResourceModule::Scan()
{
    candidates_.clear();
    selected_ = 0;

    const std::vector<int> exact(def_.pattern, def_.pattern + def_.patternSize);
    std::vector<std::uintptr_t> addresses;

    const std::vector<ModuleRange> modules = LoadedModules();
    const HMODULE mainModule = GetModuleHandleW(nullptr);

    for (const ModuleRange& range : modules) {
        if (range.module == bd::g_module || range.system)
            continue;
        if (onlyGameExe_ && range.module != mainModule)
            continue;
        ScanModule(def_, reinterpret_cast<void*>(range.begin), range.end - range.begin, exact,
                   addresses);
    }

    if (addresses.empty() && looseMatch_) {
        // Any "mov [r15+disp32], eax": the stub replays the original bytes, so a
        // moved counter still works - but it can also hit the wrong value.
        const std::vector<int> loose = { 0x41, 0x89, 0x87, -1, -1, 0x00, 0x00 };
        for (const ModuleRange& range : modules) {
            if (range.module == bd::g_module || range.system)
                continue;
            if (onlyGameExe_ && range.module != mainModule)
                continue;
            ScanModule(def_, reinterpret_cast<void*>(range.begin), range.end - range.begin, loose,
                       addresses);
        }
    }

    if (scanAllMemory_) {
        // Cheat Engine scans everything, including JIT'd or allocated code that
        // does not belong to any module.
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);

        auto* region = static_cast<std::uint8_t*>(systemInfo.lpMinimumApplicationAddress);
        auto* const limit = static_cast<std::uint8_t*>(systemInfo.lpMaximumApplicationAddress);

        while (region < limit) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(region, &memory, sizeof(memory)))
                break;

            const bool executable =
                (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                   PAGE_EXECUTE_WRITECOPY)) != 0;

            if (memory.State == MEM_COMMIT && executable && memory.RegionSize >= exact.size()) {
                const auto begin = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);

                bool skip = false;
                for (const ModuleRange& range : modules) {
                    if (range.module == bd::g_module)
                        continue;
                    if (range.system && begin >= range.begin && begin < range.end)
                        skip = true; // already covered, and never a Windows dll
                }

                if (!skip) {
                    std::vector<std::uintptr_t> found;
                    SafeScanRange(def_, static_cast<std::uint8_t*>(memory.BaseAddress),
                                  memory.RegionSize, exact, found);
                    for (std::uintptr_t address : found) {
                        if (std::find(addresses.begin(), addresses.end(), address) == addresses.end())
                            addresses.push_back(address);
                    }
                }
            }

            region = static_cast<std::uint8_t*>(memory.BaseAddress) + memory.RegionSize;
        }
    }

    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());

    monoDetected_ = GetModuleHandleW(L"mono-2.0-bdwgc.dll") != nullptr;
    if (monoDetected_)
        Trace(def_, log::Level::Info, "Mono is loaded, so the game code is compiled at runtime - matches "
                  "outside any module are listed first");

    // On Mono the counter lives in JIT'd code, so those come first: the same
    // bytes inside a dll are almost always an unrelated store.
    std::vector<std::uintptr_t> ordered;
    for (std::uintptr_t address : addresses) {
        if (BaseFor(address, modules) == 0)
            ordered.push_back(address);
    }
    for (std::uintptr_t address : addresses) {
        if (BaseFor(address, modules) != 0)
            ordered.push_back(address);
    }

    for (std::uintptr_t address : ordered) {
        if (candidates_.size() >= kMaxCandidates)
            break;
        Candidate candidate;
        candidate.address = address;
        candidate.label = Describe(address, modules);
        candidates_.push_back(candidate);
        Trace(def_, log::Level::Info, "match at %s", candidate.label.c_str());
    }

    if (candidates_.empty()) {
        status_ = "no match found (rescanning)";
        Trace(def_, log::Level::Error, "no match, %zu modules scanned", modules.size());
        LogModuleDump(def_, modules);
        return;
    }

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%zu match%s found - pick one below",
                  candidates_.size(), candidates_.size() == 1 ? "" : "es");
    status_ = buffer;
}

bool ResourceModule::Hook(std::uintptr_t address)
{
#ifndef _WIN64
    (void)address;
    status_ = "64-bit only - build the x64 configuration";
    Trace(def_, log::Level::Error, "this hook needs the 64-bit build");
    return false;
#else
    if (installed_)
        Remove();
    if (address == 0)
        return false;

    auto* found = reinterpret_cast<std::uint8_t*>(address);

    void* memory = AllocateNear(found, kStubSize);
    if (!memory) {
        status_ = "could not allocate memory near the game code";
        Trace(def_, log::Level::Error, "no memory within 2 GB of 0x%p", static_cast<void*>(found));
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

    std::memcpy(code + at, found, kInstructionSize); // the original mov [r15+...], eax
    at += kInstructionSize;

    code[at++] = 0x58; // pop rax
    code[at++] = 0xFF; // jmp qword ptr [rip+0]
    code[at++] = 0x25;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;
    code[at++] = 0x00;

    const std::uint64_t back = reinterpret_cast<std::uint64_t>(found + kInstructionSize);
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

    std::uint8_t patch[kInstructionSize];
    const std::int32_t jump = static_cast<std::int32_t>(relative);
    patch[0] = 0xE9;
    std::memcpy(patch + 1, &jump, sizeof(jump));
    patch[5] = 0x90;
    patch[6] = 0x90;

    DWORD protection = 0;
    {
        ThreadFreeze freeze;
        if (freeze.IsBusy(found, kInstructionSize)) {
            status_ = "waiting for a safe moment";
            VirtualFree(memory, 0, MEM_RELEASE);
            return false;
        }

        std::memcpy(original_, found, kInstructionSize);

        if (!VirtualProtect(found, kInstructionSize, PAGE_EXECUTE_READWRITE, &protection)) {
            VirtualFree(memory, 0, MEM_RELEASE);
            protection = 0;
            return false;
        }

        std::memcpy(found, patch, kInstructionSize);
        VirtualProtect(found, kInstructionSize, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), found, kInstructionSize);
    }

    if (protection == 0) {
        status_ = "VirtualProtect failed";
        Trace(def_, log::Level::Error, "VirtualProtect failed (%lu)", GetLastError());
        return false;
    }

    target_ = found;
    stub_ = memory;
    stubSize_ = kStubSize;
    installed_ = true;

    char buffer[192];
    std::snprintf(buffer, sizeof(buffer), "hooked at %s",
                  Describe(address, LoadedModules()).c_str());
    status_ = buffer;
    Trace(def_, log::Level::Info, "hooked 0x%p (mode %s, amount %d)", static_cast<void*>(found),
              mode_ == Mode::Add ? "add" : "set", amount_);
    return true;
#endif
}

void ResourceModule::Remove()
{
    if (!installed_ || !target_)
        return;

    ThreadFreeze freeze;

    DWORD protection = 0;
    if (VirtualProtect(target_, kInstructionSize, PAGE_EXECUTE_READWRITE, &protection)) {
        std::memcpy(target_, original_, kInstructionSize);
        VirtualProtect(target_, kInstructionSize, protection, &protection);
        FlushInstructionCache(GetCurrentProcess(), target_, kInstructionSize);
        Trace(def_, log::Level::Info, "original bytes restored");
    } else {
        Trace(def_, log::Level::Error, "could not restore the original bytes (%lu)", GetLastError());
    }

    // The stub is deliberately not freed: a thread can still be sitting in it.
    stub_ = nullptr;
    target_ = nullptr;
    installed_ = false;
    status_ = candidates_.empty() ? "not scanned yet" : "hook removed";
}

void ResourceModule::Reinstall()
{
    if (!installed_ || !target_)
        return;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(target_);
    Remove();
    Hook(address);
}

void ResourceModule::OnMenu()
{
    if (installed_) {
        ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.45f, 1.0f), "%s", status_.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.30f, 1.0f), "%s", status_.c_str());
    }

    ImGui::TextDisabled("Windows dlls are never scanned or patched.");

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

    if (!installed_) {
        ImGui::Spacing();

        if (candidates_.empty()) {
            if (monoDetected_)
                ImGui::TextWrapped("This game runs on Mono, so the code is compiled while you "
                                   "play: pick up or spend a stone first, then press Scan, or the "
                                   "instruction does not exist yet. Any Cheat Engine script for "
                                   "this address must be turned off.");
            else
                ImGui::TextDisabled("Turn any Cheat Engine script for this address off, then "
                                    "press Scan.");
            if (ImGui::Button("Scan for the instruction"))
                Scan();
        } else {
            std::vector<std::string> labels;
            for (const Candidate& candidate : candidates_)
                labels.push_back(candidate.label);

            std::vector<const char*> items;
            for (const std::string& label : labels)
                items.push_back(label.c_str());

            if (selected_ >= static_cast<int>(candidates_.size()))
                selected_ = 0;

            ImGui::Combo("Match", &selected_, items.data(), static_cast<int>(items.size()));

            if (ImGui::Button("Hook this one")) {
                if (Hook(candidates_[static_cast<std::size_t>(selected_)].address))
                    app::MarkSettingsDirty();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("starts at the first match");
        }

        if (ImGui::Button("Scan again")) {
            Scan();
            app::MarkSettingsDirty();
        }

        if (ui::Toggle("Only scan BananaDrama.exe", &onlyGameExe_,
                       "On: only the game exe is searched. Off: every non-Windows dll as well.")) {
            Scan();
            app::MarkSettingsDirty();
        }

        if (ui::Toggle("Scan all memory", &scanAllMemory_,
                       "Also search code that does not belong to a module, like Cheat Engine "
                       "does. Needed for Mono/JIT'd game code.")) {
            Scan();
            app::MarkSettingsDirty();
        }

        if (ui::Toggle("Loose match", &looseMatch_,
                       "Any mov [r15+disp32], eax instead of only 0x578. Can hook the wrong "
                       "value.")) {
            Scan();
            app::MarkSettingsDirty();
        }

        if (ui::Toggle("Auto-hook", &autoHook_,
                       "Hook the selected match as soon as the module is switched on. Only turn "
                       "this on once you know which match is the right one."))
            app::MarkSettingsDirty();
    } else {
        if (ImGui::Button("Unhook")) {
            Remove();
            app::MarkSettingsDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Re-hook"))
            Reinstall();
    }

    ImGui::TextDisabled("%d frames since it was switched on", frames_);
    if (def_.guessed)
        ImGui::TextWrapped("No Cheat Engine script for this one: the offset is a guess, so "
                           "check the match list before hooking.");

    ImGui::TextDisabled("Single-player / offline only.");
}

void ResourceModule::OnSave(json::Value& out) const
{
    out.set("mode", json::Value(static_cast<int>(mode_)));
    out.set("amount", json::Value(amount_));
    out.set("looseMatch", json::Value(looseMatch_));
    out.set("scanAllMemory", json::Value(scanAllMemory_));
    out.set("onlyGameExe", json::Value(onlyGameExe_));
    out.set("autoHook", json::Value(autoHook_));
    out.set("selected", json::Value(selected_));
}

void ResourceModule::OnLoad(const json::Value& in)
{
    if (const json::Value* v = in.find("mode"))
        mode_ = static_cast<Mode>(v->asInt(static_cast<int>(mode_)));
    if (const json::Value* v = in.find("amount"))
        amount_ = v->asInt(amount_);
    if (const json::Value* v = in.find("looseMatch"))
        looseMatch_ = v->asBool(looseMatch_);
    if (const json::Value* v = in.find("scanAllMemory"))
        scanAllMemory_ = v->asBool(scanAllMemory_);
    if (const json::Value* v = in.find("onlyGameExe"))
        onlyGameExe_ = v->asBool(onlyGameExe_);
    if (const json::Value* v = in.find("autoHook"))
        autoHook_ = v->asBool(autoHook_);
    if (const json::Value* v = in.find("selected"))
        selected_ = v->asInt(selected_);
}

namespace {

// mov [r15+0x578], eax - confirmed with Cheat Engine.
constexpr std::uint8_t kStonePattern[] = { 0x41, 0x89, 0x87, 0x78, 0x05, 0x00, 0x00 };

// mov [r15+0x570], eax plus the next instruction, mov rax,[r15+58]: the extra
// bytes are what make this one unique.
constexpr std::uint8_t kWoodPattern[] = { 0x41, 0x89, 0x87, 0x70, 0x05, 0x00, 0x00,
                                          0x49, 0x8B, 0x47 };

// mov [r15+0x574], eax plus mov rax,[r15+40]. Seven bytes were not unique here,
// which is why the following instruction is part of the pattern.
constexpr std::uint8_t kBananaPattern[] = { 0x41, 0x89, 0x87, 0x74, 0x05, 0x00, 0x00,
                                            0x49, 0x8B, 0x47, 0x40 };

// The counters sit next to each other (0x570 wood, 0x574 bananas, 0x578 stone),
// so these two are the next slots along. Guessed, not confirmed.
constexpr std::uint8_t kSilverPattern[] = { 0x41, 0x89, 0x87, 0x7C, 0x05, 0x00, 0x00 };
constexpr std::uint8_t kRubyPattern[] = { 0x41, 0x89, 0x87, 0x80, 0x05, 0x00, 0x00 };

const ResourceDef kResources[] = {
    { "stone",   "Stone",          kStonePattern,  sizeof(kStonePattern),  0x578, false },
    { "wood",    "Wood",           kWoodPattern,   sizeof(kWoodPattern),   0x570, false },
    { "bananas", "Bananas",        kBananaPattern, sizeof(kBananaPattern), 0x574, false },
    { "silver",  "Silver bananas", kSilverPattern, sizeof(kSilverPattern), 0x57C, true },
    { "ruby",    "Ruby bananas",   kRubyPattern,   sizeof(kRubyPattern),   0x580, true },
};

} // namespace

void RegisterResourceModules(ModuleManager& manager)
{
    for (const ResourceDef& def : kResources)
        manager.Register(std::make_unique<ResourceModule>(def));
}

} // namespace bd
