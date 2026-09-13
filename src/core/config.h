#pragma once

#include <string>

#include "settings.h"
#include "util/json.h"

namespace bd::config {

// The whole config file: global settings plus one object per module.
struct Document {
    Settings settings;
    json::Value modules = json::Value::Object();
};

// Full path of the json file.
std::string Path();

// Reads the file into `doc`. Returns false when there is no file yet
// (the document then keeps the built-in defaults).
bool Load(Document& doc);
bool Save(const Document& doc);

// Deletes the file so the next start-up falls back to defaults.
bool ResetToDefaults();

} // namespace bd::config
