#include "module.h"

namespace bd {

Module::Module(std::string id, std::string displayName, std::string description)
    : id_(std::move(id))
    , displayName_(std::move(displayName))
    , description_(std::move(description))
{
}

// Default implementations: a module with no options inherits these, and a
// module with options can call them from its own override if it wants to.
void Module::OnSave(json::Value& out) const
{
    (void)out;
}

void Module::OnLoad(const json::Value& in)
{
    (void)in;
}

} // namespace bd
