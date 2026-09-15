#pragma once

#include "../core.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "module.h"

namespace bd {

// Boosts the stone counter on its way into memory.
//
// The game updates the counter with:
//
//     mov [r15+0x578], eax       41 89 87 78 05 00 00
//
// Switching the module on only *searches*. It lists what it found and waits for
// you to confirm, because patching the wrong copy of those bytes (a system dll,
// or a second copy Cheat Engine left behind) crashes the game. Once you know
// which entry is the right one, tick "Auto-hook" to skip the confirmation.
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

    struct Candidate {
        std::uintptr_t address = 0;
        std::string label; // module name, or "memory" when it sits outside one
    };

    // Fills candidates_ and logs every hit.
    void Scan();
    bool Hook(std::uintptr_t address);
    void Remove();
    void Reinstall();

    Mode mode_ = Mode::Add;
    int amount_ = 999999;
    bool looseMatch_ = false;      // any mov [r15+disp32], eax, not just 0x578
    bool scanAllMemory_ = true;    // include JIT'd code - Mono compiles the game at runtime
    bool onlyGameExe_ = false;     // the instruction is rarely in the exe on Mono
    bool monoDetected_ = false;    // mono-2.0-bdwgc.dll: game code is JIT'd
    bool autoHook_ = false;        // hook without asking first

    std::vector<Candidate> candidates_;
    int selected_ = 0;

    std::uint8_t* target_ = nullptr;
    void* stub_ = nullptr;
    std::size_t stubSize_ = 0;
    std::uint8_t original_[7]{};
    bool installed_ = false;
    int frames_ = 0;
    std::string status_ = "not scanned yet";
};

} // namespace bd
