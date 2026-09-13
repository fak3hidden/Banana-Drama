#include "paths.h"

#include "../core.h"
#include "string_conv.h"

#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <shellapi.h>
#include <windows.h>

namespace bd::paths {
namespace {

std::string Join(const std::string& directory, const std::string& leaf)
{
    return (std::filesystem::path(directory) / leaf).make_preferred().string();
}

std::string RoamingAppData()
{
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH) > 0)
        return Narrow(buffer);

    // Last resort: keep the files next to the dll.
    return ModuleDirectory();
}

} // namespace

std::string RootDirectory()
{
    static std::string cached;
    if (cached.empty())
        cached = Join(RoamingAppData(), "BananaDrama");
    return cached;
}

std::string ConfigPath()
{
    static std::string cached;
    if (cached.empty())
        cached = Join(RootDirectory(), "config.json");
    return cached;
}

std::string LogPath()
{
    static std::string cached;
    if (cached.empty())
        cached = Join(RootDirectory(), "banana-drama.log");
    return cached;
}

std::string ModulePath()
{
    static std::string cached;
    if (cached.empty()) {
        wchar_t buffer[MAX_PATH]{};
        const HMODULE self = bd::g_module; // set by DllMain; null falls back to the .exe
        const DWORD length = GetModuleFileNameW(self, buffer, MAX_PATH);
        cached = length ? Narrow(std::wstring(buffer, length)) : std::string("<unknown>");
    }
    return cached;
}

std::string ModuleDirectory()
{
    return std::filesystem::path(ModulePath()).parent_path().string();
}

bool ShowInExplorer(const std::string& path)
{
    if (path.empty())
        return false;

    std::filesystem::path target(path);
    std::error_code ec;
    if (std::filesystem::is_regular_file(target, ec))
        target = target.parent_path();
    target = std::filesystem::absolute(target, ec);

    const std::wstring wide = Widen(target.string());
    const HINSTANCE result = ShellExecuteW(nullptr, L"explore", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
}

} // namespace bd::paths
