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
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace {

constexpr DWORD kWaitForever = 0xFFFFFFFF; // caller prints its own timeout message

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
        std::printf("LoadLibraryW returned NULL - check the log file in %%APPDATA%%\\BananaDrama\n");
        return 1;
    }

    std::printf("Injected. LoadLibraryW returned module base 0x%llX\n",
                static_cast<unsigned long long>(exitCode));
    std::printf("Press Insert in the game to open the menu, End to unload.\n");
    return 0;
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

    if (argc < 2) {
        PrintUsage(argv[0]);
        std::printf("Running processes:\n\n");
        ListProcesses();
        return 0;
    }

    if (!EnableDebugPrivilege())
        std::printf("Note: SeDebugPrivilege was not granted (usually fine for your own games).\n");

    std::wstring target = argv[1];
    std::wstring dllPath;
    bool waitForProcess = false;

    for (int i = 2; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--wait" || argument == L"-w")
            waitForProcess = true;
        else if (dllPath.empty())
            dllPath = argument;
    }

    if (dllPath.empty())
        dllPath = DefaultDllPath();

    if (!FileExists(dllPath)) {
        std::wprintf(L"Dll not found: %s\n", dllPath.c_str());
        std::printf("  build the project first, or pass the full path as the second argument.\n");
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
            return 1;
        }
        if (ids.size() > 1)
            std::printf("Found %zu matching processes, using pid %lu.\n", ids.size(), ids[0]);
        processId = ids[0];
    }

    std::printf("Target pid: %lu\n", processId);
    return static_cast<int>(Inject(processId, dllPath));
}
