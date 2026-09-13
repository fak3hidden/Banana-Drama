#pragma once

#include <memory>
#include <vector>

#include "../util/json.h"
#include "module.h"

namespace bd {

class ModuleManager {
public:
    static ModuleManager& Get();

    // Takes ownership. Ids must be unique and stable: they are the config keys.
    void Register(std::unique_ptr<Module> module);

    Module* Find(const std::string& id) const;
    const std::vector<std::unique_ptr<Module>>& All() const { return modules_; }

    void OnFrame();       // enabled modules only
    void OnDrawOverlay(); // enabled modules only

    json::Value SaveAll() const;
    void LoadAll(const json::Value& root);

private:
    ModuleManager() = default;
    std::vector<std::unique_ptr<Module>> modules_;
};

// Registers every module shipped with the dll.
void RegisterBuiltinModules(ModuleManager& manager);

} // namespace bd
