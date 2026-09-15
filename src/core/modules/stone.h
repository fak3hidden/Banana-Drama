#pragma once

#include "../core.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "module.h"

namespace bd {

// Boosts the stone counter on its way into memory.
//
// The game updates the counter with:
//
//     mov [r15+0x578], eax       41 89 87 78 05 00 00
//
// so we scan the game module for those bytes, divert them into a small stub and
// add an amount to eax before the value is stored. The stub keeps every other
// register and the flags untouched, and jumps straight back afterwards.
//
// Ported from the Cheat Engine table in the README. 64-bit only: on a 32-bit
// build the pattern will not be found and the module says so.
class StoneModule : public Module {
public:
    StoneModule();
    ~StoneModule() override;

    void OnEnable() override;
    void OnDisable() override;
    void OnFrame() override;
    void OnMenu() override;
    void OnSave(json::Value& out) const override;
    void OnLoad(const json::Value& in) override;

private:
    enum class Mode { Add = 0, Set = 1 };

    bool Install();
    void Remove();
    void Reinstall();

    Mode mode_ = Mode::Add;
    int amount_ = 999999;
    bool looseMatch_ = false; // off by default: the loose scan can hook the wrong value
    int matchCount_ = 0;

    HMODULE module_ = nullptr;
    std::uint8_t* target_ = nullptr;
    void* stub_ = nullptr;
    std::size_t stubSize_ = 0;
    std::uint8_t original_[7]{};
    bool installed_ = false;
    int frames_ = 0;
    std::string status_ = "not installed";
};

} // namespace bd
