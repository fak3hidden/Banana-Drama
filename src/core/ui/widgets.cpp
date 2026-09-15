#include "widgets.h"

#include "../hotkey.h"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

namespace bd::ui {

void HelpMarker(const char* description)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool Toggle(const char* label, bool* value, const char* help)
{
    ImGui::PushID(label);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
    const bool changed = ImGui::Checkbox(label, value);
    if (help) {
        ImGui::SameLine();
        HelpMarker(help);
    }
    ImGui::PopID();
    return changed;
}

void KeyValue(const char* label, const char* value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    const float width = ImGui::CalcTextSize(value).x;
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
    ImGui::TextDisabled("%s", value);
}

void Section(const char* label)
{
    ImGui::Spacing();
    ImGui::SeparatorText(label);
    ImGui::Spacing();
}

bool KeyBind(const char* label, int* vKey)
{
    static ImGuiID capturing = 0;

    const ImGuiID id = ImGui::GetID(label);
    const bool isCapturing = capturing == id;

    char text[48];
    if (isCapturing)
        std::snprintf(text, sizeof(text), "press any key...");
    else
        std::snprintf(text, sizeof(text), "%s", hotkey::Name(*vKey));

    ImGui::PushID(label);
    ImGui::PushStyleColor(ImGuiCol_Button, isCapturing
                              ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]
                              : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    const bool clicked = ImGui::Button(text, ImVec2(160.0f * ImGui::GetIO().FontGlobalScale, 0.0f));
    ImGui::PopStyleColor();
    ImGui::PopID();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    if (clicked) {
        capturing = id;
        return false;
    }
    if (!isCapturing)
        return false;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { // cancel
        capturing = 0;
        return false;
    }

    int pressed = 0;
    if (hotkey::AnyPressed(&pressed) && pressed != 0) {
        *vKey = pressed;
        capturing = 0;
        return true;
    }
    return false;
}

} // namespace bd::ui
