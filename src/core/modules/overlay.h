#pragma once

#include <array>
#include <chrono>

#include "../settings.h"
#include "module.h"

namespace bd {

// The always-on HUD: fps, frame time, session clock, process id.
// Everything it draws is local information - it reads no game memory.
class OverlayModule : public Module {
public:
    OverlayModule();

    void OnFrame() override;
    void OnDrawOverlay() override;
    void OnMenu() override;
    void OnSave(json::Value& out) const override;
    void OnLoad(const json::Value& in) override;

private:
    // Options (saved in config.json under "modules": { "overlay": ... }).
    bool showFps_ = true;
    bool showFrameTime_ = false;
    bool showClock_ = true;
    bool showProcess_ = false;
    OverlayCorner corner_ = OverlayCorner::TopRight;
    float backgroundAlpha_ = 0.45f;

    // Frame timing.
    static constexpr std::size_t kSampleCount = 60;
    std::array<float, kSampleCount> frameTimes_{};
    std::size_t sampleIndex_ = 0;
    std::size_t sampleCount_ = 0;
    std::chrono::steady_clock::time_point lastFrame_;
    std::chrono::steady_clock::time_point sessionStart_;
    bool firstFrame_ = true;
};

} // namespace bd
