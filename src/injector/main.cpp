// BananaDrama.Injector.exe - loads the dll into a running process.
//
//   BananaDrama.Injector.exe BananaDrama.exe
//   BananaDrama.Injector.exe 12345 C:\path\BananaDrama.dll
//   BananaDrama.Injector.exe BananaDrama.exe --wait      (wait for the process to start)
//   BananaDrama.Injector.exe                             (list running processes)
//
// Classic LoadLibraryW + CreateRemoteThread injection: simple, and easy to
// unload again with the menu's Unload key.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace {

// The game this injector is for: with no arguments it goes straight for it.
constexpr const wchar_t* kDefaultProcess = L"Banana Drama.exe";

std::wstring Widen(const std::string& narrow)
{
    if (narrow.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()), wide.data(), size);
    return wide;
}

std::string Narrow(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), size,
                        nullptr, nullptr);
    return narrow;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string ErrorText(DWORD error)
{
    if (error == 0)
        return "success";

    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message), 0, nullptr);

    std::string text = "unknown error";
    if (length && message) {
        text.assign(message, length);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
    }
    if (message)
        LocalFree(message);
    return text + " (" + std::to_string(error) + ")";
}

// SeDebugPrivilege lets us open processes that do not belong to this user.
bool EnableDebugPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Luid = luid;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const bool ok = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr,
                                          nullptr) && GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    CloseHandle(token);
    return ok;
}

std::vector<DWORD> FindProcessIds(const std::wstring& name)
{
    std::vector<DWORD> ids;
    const std::string wanted = Lower(Narrow(name));

    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return ids;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const std::string exe = Lower(Narrow(entry.szExeFile));
            if (exe == wanted || (wanted.size() > 4 && exe.size() >= wanted.size() &&
                                  exe.compare(exe.size() - wanted.size(), wanted.size(), wanted) == 0))
                ids.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return ids;
}

void ListProcesses()
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        std::printf("Could not list processes: %s\n", ErrorText(GetLastError()).c_str());
        return;
    }

    std::vector<std::pair<DWORD, std::string>> rows;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            rows.emplace_back(entry.th32ProcessID, Narrow(entry.szExeFile));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return Lower(a.second) < Lower(b.second);
    });

    std::printf("%6s  %s\n", "PID", "Process");
    for (const auto& row : rows)
        std::printf("%6lu  %s\n", row.first, row.second.c_str());
}

std::wstring DefaultDllPath()
{
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer, length);
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path.resize(slash + 1);
#ifdef _WIN64
    path += L"BananaDrama.dll";
#else
    path += L"BananaDrama32.dll";
#endif
    return path;
}

bool FileExists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsWow64(HANDLE process, bool& wow64)
{
    BOOL result = FALSE;
    const bool ok = IsWow64Process(process, &result);
    wow64 = ok && result == TRUE;
    return ok;
}

using NtCreateThreadExFn = long(__stdcall*)(void**, unsigned long, void*, void*, void*, void*,
                                            unsigned long, std::size_t, std::size_t, std::size_t, void*);

// CreateRemoteThread is refused across integrity levels and sessions where
// NtCreateThreadEx still works - which is why Cheat Engine's injector sometimes
// succeeds where a plain CreateRemoteThread does not. Try it first.
HANDLE StartRemoteThread(HANDLE process, void* start, void* argument)
{
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        const auto ntCreateThreadEx =
            reinterpret_cast<NtCreateThreadExFn>(GetProcAddress(ntdll, "NtCreateThreadEx"));
        if (ntCreateThreadEx) {
            HANDLE thread = nullptr;
            const long status = ntCreateThreadEx(&thread, 0x001FFFFF, nullptr, process, start,
                                                 argument, 0, 0, 0, 0, nullptr);
            if (status >= 0 && thread)
                return thread;
            if (thread)
                CloseHandle(thread);
            std::printf("  NtCreateThreadEx failed (0x%08lX), trying CreateRemoteThread\n",
                        static_cast<unsigned long>(status));
        }
    }

    return CreateRemoteThread(process, nullptr, 0,
                              reinterpret_cast<LPTHREAD_START_ROUTINE>(start), argument, 0, nullptr);
}

// Reads the import table of a dll so a failed load can be explained: a dll that
// needs a module that is not installed simply returns NULL from LoadLibraryW
// with no other clue.
std::vector<std::string> ImportedModules(const std::wstring& dllPath)
{
    std::vector<std::string> names;

    const HANDLE file = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return names;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 4096) {
        CloseHandle(file);
        return names;
    }

    std::vector<char> image(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, image.data(), static_cast<DWORD>(image.size()), &read, nullptr) || read < 4096) {
        CloseHandle(file);
        return names;
    }
    CloseHandle(file);

    const char* const begin = image.data();
    const char* const limit = begin + image.size();

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(begin);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return names;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(begin + dos->e_lfanew);
    if (reinterpret_cast<const char*>(nt) + sizeof(*nt) > limit || nt->Signature != IMAGE_NT_SIGNATURE)
        return names;

    const bool pe64 = nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    const DWORD importRva = pe64
        ? reinterpret_cast<const IMAGE_NT_HEADERS64*>(nt)->OptionalHeader
              .DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress
        : nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importRva)
        return names;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    const WORD sections = nt->FileHeader.NumberOfSections;

    const auto offsetOf = [&](DWORD rva) -> DWORD {
        for (WORD i = 0; i < sections; ++i) {
            const DWORD start = section[i].VirtualAddress;
            const DWORD span = section[i].Misc.VirtualSize ? section[i].Misc.VirtualSize
                                                           : section[i].SizeOfRawData;
            if (rva >= start && rva < start + span)
                return section[i].PointerToRawData + (rva - start);
        }
        return 0;
    };

    const DWORD importOffset = offsetOf(importRva);
    if (!importOffset || importOffset + sizeof(IMAGE_IMPORT_DESCRIPTOR) > image.size())
        return names;

    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(begin + importOffset);
    for (; descriptor->Name; ++descriptor) {
        if (reinterpret_cast<const char*>(descriptor) + sizeof(*descriptor) > limit)
            break;
        const DWORD nameOffset = offsetOf(descriptor->Name);
        if (!nameOffset || nameOffset >= image.size())
            continue;
        const char* name = begin + nameOffset;
        if (name >= limit)
            continue;
        if (std::strlen(name) == 0 || strlen(name) > 255)
            continue;
        names.emplace_back(name);
    }
    return names;
}

// Tries every module the dll needs, and says which one is missing.
void ReportDependencies(const std::wstring& dllPath)
{
    const std::vector<std::string> names = ImportedModules(dllPath);
    if (names.empty()) {
        std::printf("  Could not read the dll's imports - is the file a valid 64-bit dll?\n");
        return;
    }

    std::printf("\n  Modules this dll needs:\n");
    bool missing = false;
    for (const std::string& name : names) {
        const std::wstring wide(name.begin(), name.end());
        HMODULE module = GetModuleHandleW(wide.c_str());
        const char* state = "already loaded";
        if (!module) {
            module = LoadLibraryW(wide.c_str());
            state = module ? "loads on demand" : "MISSING";
        }
        if (!module)
            missing = true;
        std::printf("    %-28s %s\n", name.c_str(), state);
    }

    if (missing) {
        std::printf("\n  A module is missing, so the game cannot load the dll. Install the\n");
        std::printf("  Visual C++ redistributable that matches your Visual Studio version.\n");
    }
}

DWORD Inject(DWORD processId, const std::wstring& dllPath)
{
    constexpr DWORD kFullRights = PROCESS_ALL_ACCESS;
    constexpr DWORD kNormalRights = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                    PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

    HANDLE process = OpenProcess(kNormalRights, FALSE, processId);
    if (!process)
        process = OpenProcess(kFullRights, FALSE, processId);
    if (!process)
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, processId);
    if (!process) {
        std::printf("OpenProcess(%lu) failed: %s\n", processId, ErrorText(GetLastError()).c_str());
        std::printf("  run this from an administrator terminal if the game was started elevated\n");
        return 1;
    }

#ifdef _WIN64
    bool targetIs32Bit = false;
    if (IsWow64(process, targetIs32Bit) && targetIs32Bit) {
        std::printf("That process is 32-bit and this injector is 64-bit.\n");
        std::printf("  build the x86 configuration and use BananaDrama32.dll instead.\n");
        CloseHandle(process);
        return 1;
    }
#endif

    const std::size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        std::printf("VirtualAllocEx failed: %s\n", ErrorText(GetLastError()).c_str());
        CloseHandle(process);
        return 1;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written)) {
        std::printf("WriteProcessMemory failed: %s\n", ErrorText(GetLastError()).c_str());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    const FARPROC loadLibrary =
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLibrary) {
        std::printf("GetProcAddress(LoadLibraryW) failed: %s\n", ErrorText(GetLastError()).c_str());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    HANDLE thread = StartRemoteThread(process, reinterpret_cast<void*>(loadLibrary), remote);
    if (!thread) {
        std::printf("Could not start a thread in the target: %s\n", ErrorText(GetLastError()).c_str());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return 1;
    }

    std::printf("Waiting for the dll to load...\n");
    const DWORD wait = WaitForSingleObject(thread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);

    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(process);

    if (wait != WAIT_OBJECT_0) {
        std::printf("The remote thread did not finish: %s\n", ErrorText(GetLastError()).c_str());
        return 1;
    }
    if (exitCode == 0) {
        std::printf("LoadLibraryW returned NULL - the game refused to load the dll.\n");
        ReportDependencies(dllPath);
        std::printf("\n  Also check the log file in %%APPDATA%%\\BananaDrama\n");
        return 1;
    }

    std::printf("Injected. LoadLibraryW returned module base 0x%llX\n",
                static_cast<unsigned long long>(exitCode));
    std::printf("Press Insert in the game to open the menu, End to unload.\n");
    return 0;
}

void Trim(std::wstring& text)
{
    const wchar_t* blanks = L" \t\r\n";
    const std::size_t first = text.find_first_not_of(blanks);
    if (first == std::wstring::npos) {
        text.clear();
        return;
    }
    const std::size_t last = text.find_last_not_of(blanks);
    text = text.substr(first, last - first + 1);
}

// Double clicking the exe from Explorer gives it a brand new console that dies
// with it, so the window vanishes before anything can be read. Detect that case
// (only our own process is attached to the console) and wait for a key.
bool StartedFromExplorer()
{
    DWORD attached[2]{};
    return GetConsoleProcessList(attached, 2) <= 1;
}

void PauseBeforeExit()
{
    if (!StartedFromExplorer())
        return;
    std::printf("\nPress Enter to close...\n");
    std::fflush(stdout);
    std::wstring ignored;
    std::getline(std::wcin, ignored);
}

void PrintUsage(const wchar_t* program)
{
    std::wstring name = program;
    const std::size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name = name.substr(slash + 1);

    std::wprintf(L"\nUsage:\n");
    std::wprintf(L"  %s <process.exe | pid> [path to dll] [--wait]\n\n", name.c_str());
    std::wprintf(L"Examples:\n");
    std::wprintf(L"  %s BananaDrama.exe\n", name.c_str());
    std::wprintf(L"  %s 12345 C:\\Mods\\BananaDrama.dll\n", name.c_str());
    std::wprintf(L"  %s BananaDrama.exe --wait   wait for the game to start\n\n", name.c_str());
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::printf("Banana Drama injector - %s\n", sizeof(void*) == 8 ? "64-bit" : "32-bit");

    std::wstring target;
    std::wstring dllPath;
    bool waitForProcess = false;

    if (argc >= 2) {
        target = argv[1];
        for (int i = 2; i < argc; ++i) {
            const std::wstring argument = argv[i];
            if (argument == L"--wait" || argument == L"-w")
                waitForProcess = true;
            else if (dllPath.empty())
                dllPath = argument;
        }
    } else {
        // No arguments: go for Banana Drama.exe when exactly one is running.
        const std::vector<DWORD> ids = FindProcessIds(kDefaultProcess);
        if (ids.size() == 1) {
            std::printf("Attaching to %ls (pid %lu)\n", kDefaultProcess, ids[0]);
            target = kDefaultProcess;
            waitForProcess = false;
        } else {
            if (ids.empty())
                std::printf("No running %ls, pick a process instead:\n", kDefaultProcess);
            else
                std::printf("Found %zu copies of %ls, pick one instead:\n", ids.size(),
                            kDefaultProcess);

            std::printf("\nRunning processes:\n\n");
            ListProcesses();

            std::printf("\nType a PID or a process name and press Enter (just Enter to quit):\n> ");
            std::wstring line;
            if (!std::getline(std::wcin, line)) {
                PauseBeforeExit();
                return 0;
            }
            Trim(line);
            if (line.empty()) {
                PauseBeforeExit();
                return 0;
            }
            target = line;
        }
    }

    if (!EnableDebugPrivilege())
        std::printf("Note: SeDebugPrivilege was not granted (usually fine for your own games).\n");

    if (dllPath.empty())
        dllPath = DefaultDllPath();

    if (!FileExists(dllPath)) {
        std::wprintf(L"Dll not found: %s\n", dllPath.c_str());
        std::printf("  build the project first, or pass the full path as the second argument.\n");
        PauseBeforeExit();
        return 1;
    }

    std::wprintf(L"Dll: %s\n", dllPath.c_str());

    DWORD processId = 0;
    const bool isPid = !target.empty() && target[0] >= L'0' && target[0] <= L'9';
    if (isPid) {
        processId = static_cast<DWORD>(std::wcstoul(target.c_str(), nullptr, 10));
    } else {
        std::vector<DWORD> ids = FindProcessIds(target);
        while (ids.empty() && waitForProcess) {
            std::printf("Waiting for %ls to start...\n", target.c_str());
            Sleep(1000);
            ids = FindProcessIds(target);
        }

        if (ids.empty()) {
            std::wprintf(L"No running process matches %s.\n", target.c_str());
            std::printf("  run without arguments to list the running processes.\n");
            PauseBeforeExit();
            return 1;
        }
        if (ids.size() > 1)
            std::printf("Found %zu matching processes, using pid %lu.\n", ids.size(), ids[0]);
        processId = ids[0];
    }

    std::printf("Target pid: %lu\n", processId);
    const DWORD result = Inject(processId, dllPath);
    PauseBeforeExit();
    return static_cast<int>(result);
}
