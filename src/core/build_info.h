#pragma once

#include <cstdint>
#include <string>

namespace bd::build_info {

// Everything the menu and the update check need to know about this dll.
const char* Stamp();      // "Sep 15 2026 19:42:07" straight from the compiler
std::int64_t UtcTime();   // when this dll was compiled, as unix time

// Parses GitHub's timestamps, e.g. "2026-09-15T18:33:12Z". Returns 0 on failure.
std::int64_t EpochFromIso(const std::string& iso);

} // namespace bd::build_info
