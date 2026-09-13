// Small UTF-16 <-> UTF-8 helpers so the rest of the code can stay on std::string.
#pragma once

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace bd {

inline std::string Narrow(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string narrow(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        narrow.data(), size, nullptr, nullptr);
    return narrow;
}

inline std::string Narrow(const wchar_t* wide)
{
    return wide ? Narrow(std::wstring(wide)) : std::string();
}

inline std::wstring Widen(const std::string& narrow)
{
    if (narrow.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()),
                        wide.data(), size);
    return wide;
}

} // namespace bd
