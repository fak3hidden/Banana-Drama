#pragma once

#include <string>

namespace bd::log {

enum class Level { Debug, Info, Warning, Error };

// Opens the console and/or the log file. Safe to call more than once.
void Initialize(bool console, bool file);
void Shutdown();

void Write(Level level, const char* format, ...);

void Debug(const char* format, ...);
void Info(const char* format, ...);
void Warning(const char* format, ...);
void Error(const char* format, ...);

// Console visibility (the console keeps the log running either way).
void SetConsoleVisible(bool visible);
bool IsConsoleVisible();

std::string FilePath();

} // namespace bd::log
