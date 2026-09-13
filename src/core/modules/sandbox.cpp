#include "sandbox.h"

#include "../app.h"
#include "../hotkey.h"
#include "../ui/widgets.h"
#include "imgui.h"

#include <cstdio>

namespace bd {

namespace {
const char* const kModes[] = { "Gentle", "Normal", "Spicy" };
}

SandboxModule::SandboxModule()
    : Module("sandbox", "Sandbox",
             "Every ImGui control in one place. Copy this module to start a new one.")
{
}

void SandboxModule::OnFrame()
{
    ++frames_;
}

void SandboxModule::OnDrawOverlay()
{
    if (!showPanel_)
        return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, io.DisplaySize.y - 12.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.45f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##sandbox", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(label_);
    ImGui::TextDisabled("%s - %d - %.2f - frames %d", kModes[mode_], count_, strength_, frames_);
    ImGui::ColorButton("##tint", ImVec4(tint_[0], tint_[1], tint_[2], tint_[3]),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(60.0f, 20.0f));
    ImGui::End();
}

void SandboxModule::OnMenu()
{
    bool changed = false;

    ImGui::TextDisabled("Nothing here touches the game - it is a widget template.");

    changed |= ui::Toggle("Example toggle", &enabled_);
    changed |= ImGui::SliderInt("Example slider", &count_, 1, 10);
    changed |= ImGui::SliderFloat("Example float", &strength_, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Combo("Example combo", &mode_, kModes, IM_ARRAYSIZE(kModes));
    changed |= ImGui::ColorEdit4("Example colour", tint_);
    changed |= ImGui::InputText("Example text", label_, sizeof(label_));
    changed |= ui::KeyBind("Example key", &triggerKey_);

    if (ImGui::Button("Toggle the example panel"))
        showPanel_ = !showPanel_;

    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        enabled_ = true;
        count_ = 3;
        strength_ = 0.5f;
        mode_ = 0;
        triggerKey_ = 0x46; // F
        showPanel_ = false;
        changed = true;
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%d frames seen, key is %s",
                  frames_, hotkey::Name(triggerKey_));
    ImGui::TextDisabled("%s", buffer);

    if (changed)
        app::MarkSettingsDirty();
}

void SandboxModule::OnSave(json::Value& out) const
{
    out.set("exampleToggle", json::Value(enabled_));
    out.set("count", json::Value(count_));
    out.set("strength", json::Value(static_cast<double>(strength_)));
    out.set("mode", json::Value(mode_));
    out.set("triggerKey", json::Value(triggerKey_));
    out.set("label", json::Value(std::string(label_)));
    out.set("showPanel", json::Value(showPanel_));

    json::Value tint = json::Value::Array();
    for (int i = 0; i < 4; ++i)
        tint.push(json::Value(static_cast<double>(tint_[i])));
    out.set("tint", std::move(tint));
}

void SandboxModule::OnLoad(const json::Value& in)
{
    if (const json::Value* v = in.find("exampleToggle"))
        enabled_ = v->asBool(enabled_);
    if (const json::Value* v = in.find("count"))
        count_ = v->asInt(count_);
    if (const json::Value* v = in.find("strength"))
        strength_ = v->asFloat(strength_);
    if (const json::Value* v = in.find("mode"))
        mode_ = v->asInt(mode_);
    if (const json::Value* v = in.find("triggerKey"))
        triggerKey_ = v->asInt(triggerKey_);
    if (const json::Value* v = in.find("label")) {
        const std::string text = v->asString(label_);
        std::snprintf(label_, sizeof(label_), "%s", text.c_str());
    }
    if (const json::Value* v = in.find("showPanel"))
        showPanel_ = v->asBool(showPanel_);
    if (const json::Value* v = in.find("tint")) {
        if (v->isArray() && v->size() == 4) {
            for (int i = 0; i < 4; ++i)
                tint_[i] = v->at(static_cast<std::size_t>(i)).asFloat(tint_[i]);
        }
    }
}

} // namespace bd
