#pragma once

#include "../settings.h"
#include "module.h"

namespace bd {

// A template module: it shows one of every ImGui control you are likely to
// need, keeps its own options and persists them in config.json.
//
// To write a real module:
//   1. copy this pair of files and rename the class,
//   2. give it a stable id in the constructor ("my_feature"),
//   3. do the work in OnFrame(), draw in OnDrawOverlay(),
//   4. expose the knobs in OnMenu() and persist them in OnSave/OnLoad,
//   5. register it in RegisterBuiltinModules() in module_manager.cpp.
class SandboxModule : public Module {
public:
    SandboxModule();

    void OnFrame() override;
    void OnDrawOverlay() override;
    void OnMenu() override;
    void OnSave(json::Value& out) const override;
    void OnLoad(const json::Value& in) override;

private:
    bool enabled_ = true;          // example checkbox
    int count_ = 3;                // example slider
    float strength_ = 0.5f;        // example slider
    int mode_ = 0;                 // example combo
    int triggerKey_ = 0x46;        // example key bind (F)
    float tint_[4] = { 1.0f, 0.85f, 0.3f, 1.0f }; // example colour picker
    char label_[64] = "banana";    // example text input
    bool showPanel_ = false;       // example button
    int frames_ = 0;
};

} // namespace bd
