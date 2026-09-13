#pragma once

#include <string>

namespace bd::paths {

// Everything lives under %APPDATA%\BananaDrama (falls back to the dll folder).
std::string RootDirectory();
std::string ConfigPath();
std::string LogPath();

// Full path of this dll and the folder it was injected from.
std::string ModulePath();
std::string ModuleDirectory();

// Opens a folder in Windows Explorer, or a file's parent folder.
bool ShowInExplorer(const std::string& path);

} // namespace bd::paths
