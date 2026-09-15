#pragma once

#include <string>

#include "../util/json.h"

namespace bd {

// One feature in the menu. Derive from this, register it in
// modules::RegisterBuiltins(), and the menu + config system do the rest.
//
// Modules own their own options: write them in OnSave/OnLoad and they end up
// under "modules": { "<id>": { ... } } in config.json.
class Module {
public:
    Module(std::string id, std::string displayName, std::string description = std::string());
    virtual ~Module() = default;

    const std::string& Id() const { return id_; }
    const std::string& DisplayName() const { return displayName_; }
    const std::string& Description() const { return description_; }

    bool Enabled() const { return enabled_; }
    void SetEnabled(bool enabled);

    // Switched on and off from the menu.
    virtual void OnEnable() {}
    virtual void OnDisable() {}

    // Every frame while enabled, before ImGui starts the frame.
    virtual void OnFrame() {}

    // Every frame while enabled, inside the ImGui frame: draw overlay windows.
    virtual void OnDrawOverlay() {}

    // Settings rows, drawn in the Modules tab while the module is enabled.
    virtual void OnMenu() {}

    virtual void OnSave(json::Value& out) const;
    virtual void OnLoad(const json::Value& in);

private:
    std::string id_;
    std::string displayName_;
    std::string description_;
    bool enabled_ = false;
};

inline void Module::SetEnabled(bool enabled)
{
    if (enabled == enabled_)
        return;
    enabled_ = enabled;
    if (enabled_)
        OnEnable();
    else
        OnDisable();
}

} // namespace bd
