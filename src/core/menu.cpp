#include "menu.h"

#include "app.h"
#include "config.h"
#include "core.h"
#include "hooks.h"
#include "hotkey.h"
#include "renderer.h"
#include "settings.h"
#include "ui/widgets.h"
#include "util/env.h"
#include "util/log.h"
#include "util/paths.h"
#include "modules/module_manager.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace bd::menu {
namespace {

bool g_open = false;

ImVec4 Accent(float alpha = 1.0f)
{
    return ImVec4(g_settings.accent[0], g_settings.accent[1], g_settings.accent[2],
                  alpha * g_settings.accent[3]);
}

void DrawWatermark()
{
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(12.0f, io.DisplaySize.y - 12.0f), ImGuiCond_Always,
                            ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##watermark", nullptr, flags)) {
        ImGui::End();
        return;
    }
    ImGui::TextColored(Accent(), "Banana Drama");
    ImGui::SameLine();
    ImGui::TextDisabled("%s to open", hotkey::Name(g_settings.menuKey));
    ImGui::End();
}

void DrawModulesTab()
{
    ModuleManager& manager = ModuleManager::Get();
    if (manager.All().empty()) {
        ImGui::TextDisabled("No modules registered.");
        return;
    }

    for (const auto& module : manager.All()) {
        ImGui::PushID(module->Id().c_str());

        bool enabled = module->Enabled();
        if (ui::Toggle(module->DisplayName().c_str(), &enabled)) {
            module->SetEnabled(enabled);
            app::MarkSettingsDirty();
            log::Info("modules: %s %s", module->Id().c_str(), enabled ? "on" : "off");
        }

        if (!module->Description().empty()) {
            ImGui::Indent(26.0f);
            ImGui::TextDisabled("%s", module->Description().c_str());
            ImGui::Unindent(26.0f);
        }

        if (module->Enabled()) {
            ImGui::Indent(12.0f);
            module->OnMenu();
            ImGui::Unindent(12.0f);
            ImGui::Spacing();
        } else if (!module->Description().empty()) {
            // nothing: the description above already says what it does
        }

        ImGui::Separator();
        ImGui::PopID();
    }
}

void DrawAppearanceSection()
{
    ui::Section("Appearance");

    float scale = g_settings.uiScale;
    if (ImGui::SliderFloat("Interface scale", &scale, 0.75f, 2.0f, "%.2fx")) {
        g_settings.uiScale = scale;
        renderer::ApplyStyle();
        renderer::RefreshFonts();
        app::MarkSettingsDirty();
    }

    if (ImGui::SliderFloat("Window opacity", &g_settings.uiAlpha, 0.40f, 1.0f, "%.2f")) {
        renderer::ApplyStyle();
        app::MarkSettingsDirty();
    }

    if (ImGui::ColorEdit4("Accent colour", g_settings.accent, ImGuiColorEditFlags_NoInputs)) {
        renderer::ApplyStyle();
        app::MarkSettingsDirty();
    }
    ImGui::SameLine();
    ui::HelpMarker("Tints checkboxes, sliders, headers and the title bar.");

    if (ui::Toggle("Show the watermark", &g_settings.showWatermark))
        app::MarkSettingsDirty();
    if (ui::Toggle("Open the menu on inject", &g_settings.menuOpenOnInject))
        app::MarkSettingsDirty();

    if (ImGui::Button("Reset appearance")) {
        const Settings defaults;
        g_settings.uiScale = defaults.uiScale;
        g_settings.uiAlpha = defaults.uiAlpha;
        for (int i = 0; i < 4; ++i)
            g_settings.accent[i] = defaults.accent[i];
        renderer::ApplyStyle();
        renderer::RefreshFonts();
        app::MarkSettingsDirty();
    }
}

void DrawInputSection()
{
    ui::Section("Input");

    if (ui::KeyBind("Menu key", &g_settings.menuKey))
        app::MarkSettingsDirty();
    if (ui::KeyBind("Unload key", &g_settings.unloadKey))
        app::MarkSettingsDirty();

    if (ui::Toggle("Block game input while the menu is open", &g_settings.blockGameInputWhileOpen,
                   "Off: clicks go to both the menu and the game."))
        app::MarkSettingsDirty();
}

void DrawFilesSection()
{
    ui::Section("Files");

    ImGui::TextDisabled("%s", config::Path().c_str());

    if (ImGui::Button("Open folder"))
        paths::ShowInExplorer(config::Path());
    ImGui::SameLine();
    if (ImGui::Button("Save now"))
        app::SaveConfig();
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
        app::LoadConfig();
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        config::ResetToDefaults();
        g_settings = Settings();
        renderer::ApplyStyle();
        renderer::RefreshFonts();
        app::SaveConfig();
        log::Info("config: reset to defaults");
    }

    ImGui::Spacing();
    if (ui::Toggle("Log to file", &g_settings.logToFile))
        app::MarkSettingsDirty();
    if (ui::Toggle("Debug console", &g_settings.consoleEnabled,
                   "A console window next to the game. It shows the same lines as the log file.")) {
        if (g_settings.consoleEnabled)
            log::Initialize(true, false);
        else
            log::SetConsoleVisible(false);
        app::MarkSettingsDirty();
    }
}

void DrawSettingsTab()
{
    DrawAppearanceSection();
    DrawInputSection();
    DrawFilesSection();
}

void DrawDebugTab()
{
    const env::Info& info = env::Get();

    ui::Section("Process");
    ui::KeyValue("Executable", info.executableName.c_str());
    ImGui::TextWrapped("%s", info.executablePath.c_str());

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%lu", info.processId);
    ui::KeyValue("Process id", buffer);
    std::snprintf(buffer, sizeof(buffer), "0x%p", reinterpret_cast<void*>(info.baseAddress));
    ui::KeyValue("Base address", buffer);
    ui::KeyValue("Bits", info.is64Bit ? "64-bit" : "32-bit");
    ui::KeyValue("Engine guess", info.engineGuess.c_str());

    ui::Section("Rendering");
    ui::KeyValue("Renderer", info.rendererGuess.c_str());

    std::string modules;
    for (const std::string& module : info.graphicsModules) {
        if (!modules.empty())
            modules += ", ";
        modules += module;
    }
    ui::KeyValue("Modules", modules.empty() ? "none found" : modules.c_str());

    if (info.rendererGuess != "Direct3D 11") {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
        ImGui::TextWrapped("%s", info.rendererNote.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Refresh"))
        env::Refresh();

    ui::Section("Overlay");
    ui::KeyValue("Dear ImGui", IMGUI_VERSION);
    std::snprintf(buffer, sizeof(buffer), "%.0f fps (%.2f ms)", ImGui::GetIO().Framerate,
                  1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
    ui::KeyValue("Frame", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(ModuleManager::Get().All().size()));
    ui::KeyValue("Modules", buffer);
    ui::KeyValue("Attached", hooks::IsAttached() ? "yes" : "no (no Present call yet)");

    if (ui::Toggle("Dear ImGui demo window", &g_settings.showDemoWindow,
                   "The official widget gallery - handy when writing new UI."))
        app::MarkSettingsDirty();

    ui::Section("Danger zone");
    if (ImGui::Button("Unload the dll"))
        app::RequestUnload();
    ImGui::SameLine();
    ImGui::TextDisabled("or press %s", hotkey::Name(g_settings.unloadKey));
}

void DrawMainWindow()
{
    const ImGuiIO& io = ImGui::GetIO();

    const float width = std::min(560.0f * g_settings.uiScale, std::max(320.0f, io.DisplaySize.x - 40.0f));
    const float height = std::min(480.0f * g_settings.uiScale, std::max(240.0f, io.DisplaySize.y - 40.0f));
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool open = g_open;
    if (!ImGui::Begin("Banana Drama", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        g_open = open;
        return;
    }

    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Modules")) {
            DrawModulesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            DrawSettingsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")) {
            DrawDebugTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
    g_open = open;
}

} // namespace

void Open() { g_open = true; }
void Close() { g_open = false; }
void Toggle() { g_open = !g_open; }
bool IsOpen() { return g_open; }

bool ShouldBlockGameInput()
{
    return g_open && g_settings.blockGameInputWhileOpen;
}

void Render()
{
    ImGuiIO& io = ImGui::GetIO();

    // The game usually hides its cursor, so ImGui draws one while we are open.
    io.MouseDrawCursor = g_open;

    if (g_settings.showWatermark)
        DrawWatermark();

    if (g_open)
        DrawMainWindow();

    if (g_settings.showDemoWindow)
        ImGui::ShowDemoWindow(&g_settings.showDemoWindow);
}

} // namespace bd::menu
