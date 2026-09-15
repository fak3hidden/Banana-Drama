#pragma once

#include <string>

namespace bd::update_check {

enum class State { Idle, UpToDate, Outdated, Failed };

struct Info {
    std::string sha;      // full commit hash, the menu trims it
    std::string date;     // GitHub's timestamp, e.g. 2026-09-15T18:33:12Z
    std::string message;  // first line of the commit message
};

// Asks GitHub what the newest commit on this branch is and compares its date
// with when the running dll was compiled. Blocks for a moment, so only call it
// from a button press - it does not spawn threads inside the game.
void Check();

State GetState();
const Info& GetInfo();
const std::string& GetError();

} // namespace bd::update_check
