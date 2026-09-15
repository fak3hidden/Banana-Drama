#pragma once

#include "../core.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "module.h"

namespace bd {

class ModuleManager;

// One of the counters the game stores with
//
//     mov [r15+0x570], eax        41 89 87 70 05 00 00
//
// A pattern can be longer than the seven bytes that get replaced: the extra
// bytes are only there to make the scan unique, they are never modified.
// Which register holds the value on its way into the counter.
enum class ValueReg { Eax, Ecx };

struct ResourceDef {
    const char* id = "";
    const char* label = "";
    const std::uint8_t* pattern = nullptr;
    std::size_t patternSize = 0;

    // How many bytes the store takes, and therefore how many get replaced:
    // 7 for mov [r15+disp32], eax   (41 89 87 .. .. .. ..)
    // 6 for mov [rax+disp32], ecx   (89 88 .. .. .. ..)
    std::size_t instructionSize = 7;
    ValueReg value = ValueReg::Eax;

    std::uint32_t offset = 0;  // the field offset, shown in the menu and the log
    bool guessed = false;      // no Cheat Engine script confirmed this one
};

// Boosts one of the resource counters on its way into memory.
//
// Switching the module on only *searches*. It lists what it found and waits for
// you to confirm, because patching the wrong copy of those bytes (a system dll,
// or a second copy Cheat Engine left behind) crashes the game. Once you know
// which entry is the right one, tick "Auto-hook" to skip the confirmation.
class ResourceModule : public Module {
public:
    explicit ResourceModule(const ResourceDef& def);
    ~ResourceModule() override;

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
        std::string label;  // module name, or jit 0x<base>+0x<offset> when outside one
    };

    static std::string Description(const ResourceDef& def);

    // Fills candidates_ and logs every hit.
    void Scan();
    bool Hook(std::uintptr_t address);
    void Remove();
    void Reinstall();

    ResourceDef def_;

    Mode mode_ = Mode::Add;
    int amount_ = 999999;
    bool looseMatch_ = false;    // any mov [r15+disp32], eax, not just this offset
    bool scanAllMemory_ = true;  // include JIT'd code - Mono compiles the game at runtime
    bool onlyGameExe_ = false;   // the instruction is rarely in the exe on Mono
    bool monoDetected_ = false;  // mono-2.0-bdwgc.dll: game code is JIT'd
    bool autoHook_ = false;      // hook without asking first

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

// Registers stone, wood, bananas, silver bananas and ruby bananas.
void RegisterResourceModules(ModuleManager& manager);

} // namespace bd
