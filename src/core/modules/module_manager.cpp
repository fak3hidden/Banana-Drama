#include "module_manager.h"

#include "../util/log.h"

#include "overlay.h"
#include "sandbox.h"

namespace bd {

ModuleManager& ModuleManager::Get()
{
    static ModuleManager instance;
    return instance;
}

void ModuleManager::Register(std::unique_ptr<Module> module)
{
    if (!module)
        return;

    if (Find(module->Id())) {
        log::Warning("modules: id \"%s\" is already registered", module->Id().c_str());
        return;
    }

    log::Info("modules: registered \"%s\"", module->Id().c_str());
    modules_.push_back(std::move(module));
}

Module* ModuleManager::Find(const std::string& id) const
{
    for (const auto& module : modules_) {
        if (module->Id() == id)
            return module.get();
    }
    return nullptr;
}

void ModuleManager::OnFrame()
{
    for (const auto& module : modules_) {
        if (module->Enabled())
            module->OnFrame();
    }
}

void ModuleManager::OnDrawOverlay()
{
    for (const auto& module : modules_) {
        if (module->Enabled())
            module->OnDrawOverlay();
    }
}

json::Value ModuleManager::SaveAll() const
{
    json::Value root = json::Value::Object();
    for (const auto& module : modules_) {
        json::Value state = json::Value::Object();
        state.set("enabled", json::Value(module->Enabled()));
        module->OnSave(state);
        root.set(module->Id(), std::move(state));
    }
    return root;
}

void ModuleManager::LoadAll(const json::Value& root)
{
    if (!root.isObject())
        return;

    for (const auto& module : modules_) {
        const json::Value* state = root.find(module->Id());
        if (!state || !state->isObject())
            continue;

        if (const json::Value* enabled = state->find("enabled"))
            module->SetEnabled(enabled->asBool(module->Enabled()));
        module->OnLoad(*state);
    }
}

void RegisterBuiltinModules(ModuleManager& manager)
{
    manager.Register(std::make_unique<OverlayModule>());
    manager.Register(std::make_unique<SandboxModule>());
}

} // namespace bd
