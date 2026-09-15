#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bd::env {

// What the dll can see about the process it was injected into.
struct Info {
    unsigned long processId = 0;
    std::string executablePath;
    std::string executableName;
    std::uintptr_t baseAddress = 0;
    bool is64Bit = false;

    std::vector<std::string> graphicsModules; // "d3d11.dll", "vulkan-1.dll", ...
    std::string rendererGuess;                // "Direct3D 11", "Vulkan", ...
    std::string rendererNote;                 // what to do when it is not d3d11
    std::string engineGuess;                  // "Unity", "Unreal", "Godot", ...
};

// Collected once on first call, then cached.
const Info& Get();
void Refresh();

} // namespace bd::env
