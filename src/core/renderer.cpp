#include "renderer.h"

#include "core.h"
#include "util/log.h"

#include <algorithm>
#include <filesystem>

#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace bd::renderer {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTargetView = nullptr;
HWND g_window = nullptr;
float g_fontScale = 0.0f;

ImVec4 Accent(float alpha = 1.0f)
{
    return ImVec4(g_settings.accent[0], g_settings.accent[1], g_settings.accent[2],
                  alpha * g_settings.accent[3]);
}

ImVec4 Brighten(const ImVec4& color, float amount)
{
    return ImVec4(std::min(1.0f, color.x + amount),
                  std::min(1.0f, color.y + amount),
                  std::min(1.0f, color.z + amount),
                  color.w);
}

void StyleAccent(ImGuiStyle& style)
{
    const ImVec4 accent = Accent();
    style.Colors[ImGuiCol_CheckMark]        = accent;
    style.Colors[ImGuiCol_SliderGrab]       = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = Brighten(accent, 0.12f);
    style.Colors[ImGuiCol_Header]           = ImVec4(accent.x, accent.y, accent.z, 0.32f);
    style.Colors[ImGuiCol_HeaderHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.48f);
    style.Colors[ImGuiCol_HeaderActive]     = ImVec4(accent.x, accent.y, accent.z, 0.68f);
    style.Colors[ImGuiCol_ButtonHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    style.Colors[ImGuiCol_ButtonActive]     = ImVec4(accent.x, accent.y, accent.z, 0.60f);
    style.Colors[ImGuiCol_TitleBgActive]    = ImVec4(accent.x * 0.35f, accent.y * 0.35f, accent.z * 0.35f, 1.0f);
    style.Colors[ImGuiCol_Border]           = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    style.Colors[ImGuiCol_Separator]        = ImVec4(accent.x, accent.y, accent.z, 0.22f);
}

void LoadFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.SizePixels = 16.0f * scale;

    const wchar_t* segoe = L"C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesW(segoe) != INVALID_FILE_ATTRIBUTES) {
        const std::string path = "C:\\Windows\\Fonts\\segoeui.ttf";
        io.Fonts->AddFontFromFileTTF(path.c_str(), config.SizePixels, &config);
    } else {
        log::Warning("renderer: segoeui.ttf not found, using the built-in font");
        io.Fonts->AddFontDefault(&config);
    }
    io.Fonts->Build();
    g_fontScale = scale;
}

} // namespace

bool Initialize(IDXGISwapChain* swapChain)
{
    if (!swapChain) {
        log::Error("renderer: no swap chain");
        return false;
    }

    const HRESULT deviceResult =
        swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_device));
    if (FAILED(deviceResult) || !g_device) {
        log::Error("renderer: GetDevice failed (0x%08lX)", static_cast<unsigned long>(deviceResult));
        return false;
    }
    g_device->GetImmediateContext(&g_context);

    DXGI_SWAP_CHAIN_DESC description{};
    if (SUCCEEDED(swapChain->GetDesc(&description)))
        g_window = description.OutputWindow;

    if (!g_window) {
        log::Error("renderer: the swap chain has no output window");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // we keep our own config.json
    io.LogFilename = nullptr;

    ApplyStyle();

    if (!ImGui_ImplWin32_Init(g_window)) {
        log::Error("renderer: ImGui_ImplWin32_Init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(g_device, g_context)) {
        log::Error("renderer: ImGui_ImplDX11_Init failed");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    RecreateBackBuffer(swapChain);
    g_swapChain = swapChain;

    log::Info("renderer: Dear ImGui %s up on a %ux%u swap chain (window 0x%p)",
              IMGUI_VERSION, description.BufferDesc.Width, description.BufferDesc.Height,
              static_cast<void*>(g_window));
    return true;
}

void Shutdown()
{
    ReleaseBackBuffer();

    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }

    g_swapChain = nullptr;
    g_window = nullptr;
    g_fontScale = 0.0f;
}

bool IsInitialized()
{
    return ImGui::GetCurrentContext() != nullptr && g_device != nullptr;
}

void NewFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Render()
{
    ImGui::Render();
    if (g_renderTargetView)
        g_context->OMSetRenderTargets(1, &g_renderTargetView, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ApplyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    style.Alpha = g_settings.uiAlpha;
    style.WindowRounding = 7.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    style.ScaleAllSizes(g_settings.uiScale);

    StyleAccent(style);
}

void RefreshFonts()
{
    if (!ImGui::GetCurrentContext())
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    LoadFonts(g_settings.uiScale);
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();
}

void ReleaseBackBuffer()
{
    if (g_renderTargetView) {
        g_renderTargetView->Release();
        g_renderTargetView = nullptr;
    }
}

void RecreateBackBuffer(IDXGISwapChain* swapChain)
{
    if (!g_device || !swapChain)
        return;

    ReleaseBackBuffer();

    ID3D11Texture2D* backBuffer = nullptr;
    const HRESULT result =
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
    if (SUCCEEDED(result) && backBuffer) {
        const HRESULT viewResult = g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTargetView);
        if (FAILED(viewResult))
            log::Error("renderer: CreateRenderTargetView failed (0x%08lX)",
                       static_cast<unsigned long>(viewResult));
        backBuffer->Release();
    } else {
        log::Error("renderer: GetBuffer failed (0x%08lX)", static_cast<unsigned long>(result));
    }
}

HWND Window() { return g_window; }
ID3D11Device* Device() { return g_device; }
ID3D11DeviceContext* Context() { return g_context; }

} // namespace bd::renderer
