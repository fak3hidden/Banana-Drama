// Entry point of the dll. Everything dangerous happens on its own thread:
// DllMain is inside the loader lock, so it only starts the thread and leaves.

#include "core.h"
#include "app.h"
#include "config.h"
#include "hooks.h"
#include "hotkey.h"
#include "menu.h"
#include "renderer.h"
#include "util/env.h"
#include "util/log.h"
#include "util/paths.h"
#include "modules/module_manager.h"

#include <atomic>
#include <chrono>

namespace bd {

HMODULE g_module = nullptr;
Settings g_settings;

namespace {

constexpr int kPollIntervalMs = 25;
constexpr int kStartupAttempts = 3;
constexpr int kStartupRetryDelayMs = 2000;

DWORD WINAPI MainThread(LPVOID /*parameter*/);

} // namespace

} // namespace bd

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        bd::g_module = instance;
        if (HANDLE thread = CreateThread(nullptr, 0, bd::MainThread, nullptr, 0, nullptr))
            CloseHandle(thread);
    }
    return TRUE;
}

namespace bd {
namespace {

DWORD WINAPI MainThread(LPVOID)
{
    app::Initialize(g_module);

    bool hooked = hooks::Initialize();
    for (int attempt = 1; !hooked && attempt < kStartupAttempts; ++attempt) {
        log::Warning("startup: attempt %d failed, retrying in %d ms", attempt, kStartupRetryDelayMs);
        Sleep(kStartupRetryDelayMs);
        hooked = hooks::Initialize();
    }

    if (!hooked) {
        log::Error("startup: could not hook the swap chain. Check the Debug tab: it lists the "
                   "graphics API the game actually uses.");
        app::Shutdown();
        return 0;
    }

    if (g_settings.menuOpenOnInject)
        menu::Open();

    // Hotkeys are polled here so they keep working before the first frame,
    // and while the game is not rendering at all.
    while (!app::UnloadRequested()) {
        hotkey::Update();

        if (hotkey::Pressed(g_settings.menuKey))
            menu::Toggle();
        if (hotkey::Pressed(g_settings.unloadKey))
            app::RequestUnload();

        app::Tick();
        Sleep(kPollIntervalMs);
    }

    if (hooks::IsAttached())
        hooks::WaitForUnload(3000);
    else
        hooks::Shutdown();

    Sleep(150); // let the last Present call leave our code

    app::Shutdown();
    FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

// ------------------------------------------------------------------ app layer

std::atomic<bool> g_unloadRequested{false};
std::atomic<bool> g_settingsDirty{false};
std::chrono::steady_clock::time_point g_lastChange;

} // namespace

namespace app {

void Initialize(HMODULE module)
{
    g_module = module;

    config::Document document;
    const bool loaded = config::Load(document);
    g_settings = document.settings;

    log::Initialize(g_settings.consoleEnabled, g_settings.logToFile);

    log::Info("Banana Drama loaded (%s)", paths::ModulePath().c_str());
    log::Info("build: %s %s", __DATE__, __TIME__);
    log::Info("config: %s", config::Path().c_str());
    log::Info("config: %s", loaded ? "loaded" : "no file yet, using defaults");

    const env::Info& info = env::Get();
    log::Info("process: %s (pid %lu, %s)", info.executablePath.c_str(), info.processId,
              info.is64Bit ? "64-bit" : "32-bit");
    log::Info("renderer: %s (%s)", info.rendererGuess.c_str(), info.graphicsModules.empty()
                                                                  ? "no graphics module seen yet"
                                                                  : "modules detected");

    RegisterBuiltinModules(ModuleManager::Get());
    ModuleManager::Get().LoadAll(document.modules);

    if (!loaded)
        SaveConfig();
}

void Shutdown()
{
    SaveConfig();
    log::Info("Banana Drama unloaded");
    log::Shutdown();
}

void Tick()
{
    if (!g_settingsDirty)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now - g_lastChange < std::chrono::milliseconds(600))
        return;

    g_settingsDirty = false;
    SaveConfig();
}

void MarkSettingsDirty()
{
    g_settingsDirty = true;
    g_lastChange = std::chrono::steady_clock::now();
}

void SaveConfig()
{
    config::Document document;
    document.settings = g_settings;
    document.modules = ModuleManager::Get().SaveAll();

    if (config::Save(document))
        log::Debug("config: saved");
}

void LoadConfig()
{
    config::Document document;
    if (!config::Load(document)) {
        log::Warning("config: nothing to load");
        return;
    }

    g_settings = document.settings;
    ModuleManager::Get().LoadAll(document.modules);

    if (renderer::IsInitialized()) {
        renderer::ApplyStyle();
        renderer::RefreshFonts();
    }
    log::Info("config: reloaded");
}

void RequestUnload()
{
    if (g_unloadRequested)
        return;
    g_unloadRequested = true;
    hooks::RequestUnload();
}

bool UnloadRequested()
{
    return g_unloadRequested;
}

} // namespace app
} // namespace bd
