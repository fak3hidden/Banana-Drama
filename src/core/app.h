#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace bd::app {

// Loads the config, opens the log and registers the modules.
// Call once from the worker thread before hooking anything.
void Initialize(HMODULE module);

// Writes the config back to disk and closes the log.
void Shutdown();

// Saves the config shortly after the last change. Call every tick.
void Tick();
void MarkSettingsDirty();

void SaveConfig();
void LoadConfig();

// Asks the render thread to detach so the dll can be freed.
void RequestUnload();
bool UnloadRequested();

} // namespace bd::app
