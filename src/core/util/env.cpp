#include "env.h"

#include "../core.h"
#include "string_conv.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace bd::env {
namespace {

bool IsLoaded(const wchar_t* moduleName)
{
    return GetModuleHandleW(moduleName) != nullptr;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Contains(const std::string& haystack, const std::string& needle)
{
    return Lower(haystack).find(Lower(needle)) != std::string::npos;
}

Info g_info;
bool g_ready = false;

Info Build()
{
    Info info;
    info.processId = GetCurrentProcessId();
    info.is64Bit = sizeof(void*) == 8;

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length) {
        info.executablePath = Narrow(std::wstring(path, length));
        info.executableName = std::filesystem::path(info.executablePath).filename().string();
    }
    info.baseAddress = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

    struct Probe { const wchar_t* module; const char* label; bool graphics; };
    static const Probe probes[] = {
        { L"d3d11.dll", "d3d11.dll", true },
        { L"d3d12.dll", "d3d12.dll", true },
        { L"dxgi.dll", "dxgi.dll", true },
        { L"vulkan-1.dll", "vulkan-1.dll", true },
        { L"opengl32.dll", "opengl32.dll", true },
        { L"libGLESv2.dll", "libGLESv2.dll", true },
        { L"UnityPlayer.dll", "UnityPlayer.dll", false },
        { L"GameAssembly.dll", "GameAssembly.dll", false },
        { L"mono-2.0-bdwgc.dll", "mono-2.0-bdwgc.dll", false },
    };

    bool d3d11 = false, d3d12 = false, vulkan = false, opengl = false, gles = false;
    bool unity = false, il2cpp = false, mono = false;

    for (const Probe& probe : probes) {
        if (!IsLoaded(probe.module))
            continue;
        if (probe.graphics)
            info.graphicsModules.push_back(probe.label);

        if (Contains(probe.label, "d3d11")) d3d11 = true;
        if (Contains(probe.label, "d3d12")) d3d12 = true;
        if (Contains(probe.label, "vulkan")) vulkan = true;
        if (Contains(probe.label, "opengl32")) opengl = true;
        if (Contains(probe.label, "libGLESv2")) gles = true;
        if (Contains(probe.label, "UnityPlayer")) unity = true;
        if (Contains(probe.label, "GameAssembly")) il2cpp = true;
        if (Contains(probe.label, "mono")) mono = true;
    }

    if (d3d11) {
        info.rendererGuess = "Direct3D 11";
        info.rendererNote = "Supported - the menu hooks IDXGISwapChain::Present.";
    } else if (d3d12) {
        info.rendererGuess = "Direct3D 12";
        info.rendererNote = "Not supported by this build: it needs an ExecuteCommandLists hook.";
    } else if (vulkan) {
        info.rendererGuess = "Vulkan";
        info.rendererNote = "Not supported by this build: it needs a vkQueuePresentKHR hook.";
    } else if (opengl || gles) {
        info.rendererGuess = "OpenGL / GLES";
        info.rendererNote = "Not supported by this build: it needs a wglSwapBuffers / eglSwapBuffers hook.";
    } else {
        info.rendererGuess = "unknown";
        info.rendererNote = "No known graphics module was found yet (try opening the game menu first).";
    }

    if (unity)          info.engineGuess = "Unity (Mono)";
    else if (il2cpp)    info.engineGuess = "Unity (IL2CPP)";
    else if (mono)      info.engineGuess = "Mono";
    else if (Contains(info.executableName, "Win64-Shipping.exe") ||
             Contains(info.executableName, "Win32-Shipping.exe")) info.engineGuess = "Unreal";
    else if (Contains(info.executableName, "godot")) info.engineGuess = "Godot";
    else                info.engineGuess = "unknown";

    return info;
}

} // namespace

const Info& Get()
{
    if (!g_ready)
        Refresh();
    return g_info;
}

void Refresh()
{
    // Call again later if you want a fresh look: a game often loads its
    // renderer a second or two after the process starts.
    g_info = Build();
    g_ready = true;
}

} // namespace bd::env
