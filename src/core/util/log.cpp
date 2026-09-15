#include "log.h"

#include "paths.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif

namespace bd::log {
namespace {

FILE* g_file = nullptr;
bool g_consoleAllocated = false;
bool g_consoleVisible = false;

const char* LevelName(Level level)
{
    switch (level) {
    case Level::Debug:   return "debug";
    case Level::Info:    return "info ";
    case Level::Warning: return "warn ";
    case Level::Error:   return "error";
    }
    return "info ";
}

std::string Timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    if (const std::tm* converted = std::localtime(&now))
        local = *converted;

    char buffer[16]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

void Emit(Level level, const char* format, std::va_list args)
{
    char message[2048];
    std::vsnprintf(message, sizeof(message), format, args);

    const std::string line = "[" + Timestamp() + "] [" + LevelName(level) + "] " + message + "\n";

    OutputDebugStringA(line.c_str());

    if (g_file) {
        std::fwrite(line.data(), 1, line.size(), g_file);
        std::fflush(g_file);
    }
    if (g_consoleAllocated) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    }
}

} // namespace

void Initialize(bool console, bool file)
{
    if (file && !g_file) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(paths::LogPath()).parent_path(), ec);
        g_file = std::fopen(paths::LogPath().c_str(), "w");
        if (!g_file)
            OutputDebugStringA("[banana-drama] could not open the log file for writing\n");
    }

    if (console && !g_consoleAllocated) {
        if (AllocConsole()) {
            std::freopen("CONOUT$", "w", stdout);
            std::freopen("CONOUT$", "w", stderr);
            SetConsoleTitleW(L"Banana Drama - debug console");

            // Disabling the X button stops an accidental click from killing the game,
            // and QuickEdit stops a stray click from freezing the render thread.
            if (const HWND consoleWindow = GetConsoleWindow()) {
                if (const HMENU menu = GetSystemMenu(consoleWindow, FALSE))
                    EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
            }
            if (const HANDLE input = GetStdHandle(STD_INPUT_HANDLE)) {
                DWORD mode = 0;
                if (GetConsoleMode(input, &mode))
                    SetConsoleMode(input, mode & ~ENABLE_QUICK_EDIT_MODE);
            }

            g_consoleAllocated = true;
            g_consoleVisible = true;
        }
    } else if (console && g_consoleAllocated) {
        SetConsoleVisible(true);
    }
}

void Shutdown()
{
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    if (g_consoleAllocated) {
        std::fflush(stdout);
        FreeConsole();
        g_consoleAllocated = false;
        g_consoleVisible = false;
    }
}

void Write(Level level, const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    Emit(level, format, args);
    va_end(args);
}

void Debug(const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    Emit(Level::Debug, format, args);
    va_end(args);
}

void Info(const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    Emit(Level::Info, format, args);
    va_end(args);
}

void Warning(const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    Emit(Level::Warning, format, args);
    va_end(args);
}

void Error(const char* format, ...)
{
    std::va_list args;
    va_start(args, format);
    Emit(Level::Error, format, args);
    va_end(args);
}

void SetConsoleVisible(bool visible)
{
    if (!g_consoleAllocated)
        return;
    ShowWindow(GetConsoleWindow(), visible ? SW_SHOW : SW_HIDE);
    g_consoleVisible = visible;
}

bool IsConsoleVisible()
{
    return g_consoleVisible;
}

std::string FilePath()
{
    return paths::LogPath();
}

} // namespace bd::log
