#include "overlay.h"

#include "../app.h"
#include "../core.h"
#include "../ui/widgets.h"
#include "../util/env.h"
#include "imgui.h"

#include <cstdio>

namespace bd {

OverlayModule::OverlayModule()
    : Module("overlay", "Overlay HUD",
             "Small always-on readout: fps, frame time, session clock, process id.")
{
    sessionStart_ = std::chrono::steady_clock::now();
}

void OverlayModule::OnFrame()
{
    const auto now = std::chrono::steady_clock::now();
    if (firstFrame_) {
        lastFrame_ = now;
        firstFrame_ = false;
        return;
    }

    const float delta = std::chrono::duration<float, std::milli>(now - lastFrame_).count();
    lastFrame_ = now;

    frameTimes_[sampleIndex_] = delta;
    sampleIndex_ = (sampleIndex_ + 1) % kSampleCount;
    if (sampleCount_ < kSampleCount)
        ++sampleCount_;
}

void OverlayModule::OnDrawOverlay()
{
    if (!showFps_ && !showFrameTime_ && !showClock_ && !showProcess_)
        return;

    float total = 0.0f;
    for (std::size_t i = 0; i < sampleCount_; ++i)
        total += frameTimes_[i];
    const float average = sampleCount_ ? total / static_cast<float>(sampleCount_) : 0.0f;
    const float fps = average > 0.0f ? 1000.0f / average : 0.0f;

    const ImGuiIO& io = ImGui::GetIO();
    const float pad = 12.0f;

    ImVec2 position(pad, pad);
    ImVec2 pivot(0.0f, 0.0f);
    switch (corner_) {
    case OverlayCorner::TopRight:
        position = ImVec2(io.DisplaySize.x - pad, pad);
        pivot = ImVec2(1.0f, 0.0f);
        break;
    case OverlayCorner::BottomLeft:
        position = ImVec2(pad, io.DisplaySize.y - pad);
        pivot = ImVec2(0.0f, 1.0f);
        break;
    case OverlayCorner::BottomRight:
        position = ImVec2(io.DisplaySize.x - pad, io.DisplaySize.y - pad);
        pivot = ImVec2(1.0f, 1.0f);
        break;
    case OverlayCorner::TopLeft:
    default:
        break;
    }

    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(backgroundAlpha_);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##overlay", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const ImVec4 accent(bd::g_settings.accent[0], bd::g_settings.accent[1],
                        bd::g_settings.accent[2], bd::g_settings.accent[3]);

    char buffer[64];
    if (showFps_) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", fps);
        ui::KeyValue("FPS", buffer);
    }
    if (showFrameTime_) {
        std::snprintf(buffer, sizeof(buffer), "%.2f ms", average);
        ui::KeyValue("Frame", buffer);
    }
    if (showClock_) {
        const auto elapsed = std::chrono::steady_clock::now() - sessionStart_;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld",
                      static_cast<long long>(seconds / 3600),
                      static_cast<long long>((seconds / 60) % 60),
                      static_cast<long long>(seconds % 60));
        ui::KeyValue("Session", buffer);
    }
    if (showProcess_) {
        const env::Info& info = env::Get();
        std::snprintf(buffer, sizeof(buffer), "%lu (%s)", info.processId, info.is64Bit ? "x64" : "x86");
        ui::KeyValue("Process", buffer);
        ImGui::TextColored(accent, "%s", info.executableName.c_str());
    }

    ImGui::End();
}

void OverlayModule::OnMenu()
{
    bool changed = false;

    changed |= ui::Toggle("FPS", &showFps_);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
    changed |= ui::Toggle("Frame time", &showFrameTime_);
    changed |= ui::Toggle("Session clock", &showClock_);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
    changed |= ui::Toggle("Process id", &showProcess_);

    int corner = static_cast<int>(corner_);
    const char* corners[] = { "Top left", "Top right", "Bottom left", "Bottom right" };
    if (ImGui::Combo("Corner", &corner, corners, 4))
        corner_ = static_cast<OverlayCorner>(corner);

    changed |= ImGui::SliderFloat("Background", &backgroundAlpha_, 0.0f, 1.0f, "%.2f");

    if (changed)
        app::MarkSettingsDirty();
}

void OverlayModule::OnSave(json::Value& out) const
{
    out.set("showFps", json::Value(showFps_));
    out.set("showFrameTime", json::Value(showFrameTime_));
    out.set("showClock", json::Value(showClock_));
    out.set("showProcess", json::Value(showProcess_));
    out.set("corner", json::Value(static_cast<int>(corner_)));
    out.set("backgroundAlpha", json::Value(static_cast<double>(backgroundAlpha_)));
}

void OverlayModule::OnLoad(const json::Value& in)
{
    if (const json::Value* v = in.find("showFps"))
        showFps_ = v->asBool(showFps_);
    if (const json::Value* v = in.find("showFrameTime"))
        showFrameTime_ = v->asBool(showFrameTime_);
    if (const json::Value* v = in.find("showClock"))
        showClock_ = v->asBool(showClock_);
    if (const json::Value* v = in.find("showProcess"))
        showProcess_ = v->asBool(showProcess_);
    if (const json::Value* v = in.find("corner"))
        corner_ = static_cast<OverlayCorner>(v->asInt(static_cast<int>(corner_)));
    if (const json::Value* v = in.find("backgroundAlpha"))
        backgroundAlpha_ = v->asFloat(backgroundAlpha_);
}

} // namespace bd
