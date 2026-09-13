#pragma once

// Pulled in by every file in the dll: the platform headers plus the two globals
// that the whole codebase shares.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "settings.h"

namespace bd {

// Handle of this dll, set in DllMain before the worker thread starts.
extern HMODULE g_module;

// Live settings, owned by dllmain.cpp. The menu edits this in place and
// config::Save() writes it back to disk.
extern Settings g_settings;

} // namespace bd
