#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace bd::renderer {

// Sets up ImGui on top of the game's swap chain. Called from the Present hook.
bool Initialize(IDXGISwapChain* swapChain);
void Shutdown();
bool IsInitialized();

// Call these around the menu every frame.
void NewFrame();
void Render();

// Re-applies colours/alpha/scale from bd::g_settings, and rebuilds the font
// atlas when the scale changed.
void ApplyStyle();
void RefreshFonts();

// Back buffer targets, released and recreated around a window resize.
void ReleaseBackBuffer();
void RecreateBackBuffer(IDXGISwapChain* swapChain);

HWND Window();
ID3D11Device* Device();
ID3D11DeviceContext* Context();

} // namespace bd::renderer
