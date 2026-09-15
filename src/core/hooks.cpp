#include "hooks.h"

#include "core.h"
#include "hotkey.h"
#include "menu.h"
#include "renderer.h"
#include "util/log.h"

#include <atomic>

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "modules/module_manager.h"

#pragma comment(lib, "d3d11.lib")

// imgui_impl_win32.h keeps this declaration commented out so the header stays
// free of <windows.h>; the backend docs tell you to declare it yourself.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wParam, LPARAM lParam);

namespace bd::hooks {
namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// IDXGISwapChain vtable slots. Stable across every DXGI version so far.
constexpr int kPresentIndex = 8;
constexpr int kResizeBuffersIndex = 13;

void** g_vtable = nullptr;
PresentFn g_presentOriginal = nullptr;
ResizeBuffersFn g_resizeOriginal = nullptr;

HWND g_window = nullptr;
WNDPROC g_windowProcOriginal = nullptr;

HANDLE g_unloadEvent = nullptr;
std::atomic<bool> g_unloadRequested{false};
std::atomic<bool> g_attached{false};

// Builds a 1x1 device + swap chain purely to read the vtable out of it. The
// vtable lives in the DXGI module, so it stays valid after we throw ours away.
bool ResolveVTable()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"BananaDramaDummyWindow";
    RegisterClassExW(&windowClass);

    HWND dummy = CreateWindowExW(0, windowClass.lpszClassName, L"BananaDramaDummyWindow",
                                 WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!dummy) {
        log::Error("hooks: could not create the dummy window (%lu)", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 1;
    description.BufferDesc.Width = 1;
    description.BufferDesc.Height = 1;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = dummy;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &description, &swapChain, &device, &featureLevel, &context);

    if (FAILED(result) || !swapChain) {
        log::Error("hooks: D3D11CreateDeviceAndSwapChain failed (0x%08lX)",
                   static_cast<unsigned long>(result));
        DestroyWindow(dummy);
        return false;
    }

    g_vtable = *reinterpret_cast<void***>(swapChain);

    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();

    DestroyWindow(dummy);
    UnregisterClassW(windowClass.lpszClassName, instance);
    return g_vtable != nullptr;
}

template <typename T>
bool PatchVTable(int index, void* hook, T& original)
{
    if (!g_vtable)
        return false;

    DWORD protection = 0;
    if (!VirtualProtect(&g_vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection)) {
        log::Error("hooks: VirtualProtect failed on vtable slot %d", index);
        return false;
    }
    original = reinterpret_cast<T>(g_vtable[index]);
    g_vtable[index] = hook;
    VirtualProtect(&g_vtable[index], sizeof(void*), protection, &protection);
    return true;
}

bool RestoreVTable(int index, void* original)
{
    if (!g_vtable || !original)
        return false;

    DWORD protection = 0;
    if (!VirtualProtect(&g_vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &protection))
        return false;
    g_vtable[index] = original;
    VirtualProtect(&g_vtable[index], sizeof(void*), protection, &protection);
    return true;
}

bool IsInputMessage(UINT message)
{
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_IME_CHAR:
        return true;
    default:
        return false;
    }
}

LRESULT WINAPI hkWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    // ImGui gets first look so it can drive the menu.
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam))
        return TRUE;

    // While the menu is open the game should not see clicks and keystrokes.
    if (menu::ShouldBlockGameInput() && IsInputMessage(message))
        return 0;

    return CallWindowProcW(g_windowProcOriginal, window, message, wParam, lParam);
}

void AttachToWindow(HWND window)
{
    if (!window || g_windowProcOriginal)
        return;

    g_window = window;
    g_windowProcOriginal = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hkWndProc)));

    if (g_windowProcOriginal)
        log::Info("hooks: window procedure hooked (0x%p)", reinterpret_cast<void*>(g_window));
    else
        log::Warning("hooks: SetWindowLongPtr failed (%lu)", GetLastError());
}

void DetachFromWindow()
{
    if (!g_window || !g_windowProcOriginal)
        return;

    SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_windowProcOriginal));
    g_windowProcOriginal = nullptr;
    g_window = nullptr;
}

HRESULT WINAPI hkPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
{
    static bool initFailed = false;

    if (!renderer::IsInitialized() && !initFailed) {
        if (renderer::Initialize(swapChain)) {
            AttachToWindow(renderer::Window());
            g_attached = true;
            log::Info("hooks: attached, the menu key opens the overlay");
        } else {
            initFailed = true;
        }
    }

    if (renderer::IsInitialized()) {
        ModuleManager::Get().OnFrame();

        renderer::NewFrame();
        ModuleManager::Get().OnDrawOverlay();
        menu::Render();
        renderer::Render();
    }

    // Unload from the render thread: tearing the D3D objects down here keeps
    // them on the same thread that created them.
    if (g_unloadRequested) {
        log::Info("hooks: unloading");
        renderer::Shutdown();
        DetachFromWindow();
        RestoreVTable(kPresentIndex, reinterpret_cast<void*>(g_presentOriginal));
        RestoreVTable(kResizeBuffersIndex, reinterpret_cast<void*>(g_resizeOriginal));
        g_attached = false;
        SetEvent(g_unloadEvent);
    }

    return g_presentOriginal(swapChain, syncInterval, flags);
}

HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                               UINT height, DXGI_FORMAT format, UINT flags)
{
    if (renderer::IsInitialized())
        renderer::ReleaseBackBuffer();

    const HRESULT result = g_resizeOriginal(swapChain, bufferCount, width, height, format, flags);

    if (renderer::IsInitialized())
        renderer::RecreateBackBuffer(swapChain);

    return result;
}

} // namespace

bool Initialize()
{
    g_unloadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_unloadEvent) {
        log::Error("hooks: CreateEvent failed (%lu)", GetLastError());
        return false;
    }

    if (MH_Initialize() != MH_OK)
        log::Warning("hooks: MinHook did not start (game function hooks unavailable)");
    else
        log::Info("hooks: MinHook ready for game function hooks");

    if (!ResolveVTable()) {
        log::Error("hooks: no D3D11 device could be created - is this a Direct3D 11 game?");
        return false;
    }

    if (!PatchVTable(kPresentIndex, reinterpret_cast<void*>(&hkPresent), g_presentOriginal))
        return false;
    if (!PatchVTable(kResizeBuffersIndex, reinterpret_cast<void*>(&hkResizeBuffers), g_resizeOriginal))
        return false;

    log::Info("hooks: swap chain vtable patched (Present, ResizeBuffers)");
    return true;
}

void Shutdown()
{
    DetachFromWindow();

    if (g_presentOriginal)
        RestoreVTable(kPresentIndex, reinterpret_cast<void*>(g_presentOriginal));
    if (g_resizeOriginal)
        RestoreVTable(kResizeBuffersIndex, reinterpret_cast<void*>(g_resizeOriginal));

    g_presentOriginal = nullptr;
    g_resizeOriginal = nullptr;
    g_attached = false;

    if (MH_Uninitialize() != MH_OK)
        log::Debug("hooks: MinHook was never initialised");

    if (g_unloadEvent) {
        CloseHandle(g_unloadEvent);
        g_unloadEvent = nullptr;
    }
}

void RequestUnload()
{
    g_unloadRequested = true;
}

bool WaitForUnload(unsigned long timeoutMs)
{
    if (!g_unloadEvent)
        return true;

    const DWORD result = WaitForSingleObject(g_unloadEvent, timeoutMs);
    if (result == WAIT_OBJECT_0)
        return true;

    // Nothing is rendering (minimised window, cutscene, ...): tear down here.
    renderer::Shutdown();
    DetachFromWindow();
    if (g_presentOriginal)
        RestoreVTable(kPresentIndex, reinterpret_cast<void*>(g_presentOriginal));
    if (g_resizeOriginal)
        RestoreVTable(kResizeBuffersIndex, reinterpret_cast<void*>(g_resizeOriginal));
    g_attached = false;
    return true;
}

bool IsAttached()
{
    return g_attached;
}

} // namespace bd::hooks
