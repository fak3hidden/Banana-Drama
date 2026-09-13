#include "config.h"

#include "util/log.h"
#include "util/paths.h"

#include <cstdio>
#include <filesystem>

namespace bd::config {
namespace {

constexpr int kAccentComponents = 4;

void WriteSettings(json::Value& root, const Settings& s)
{
    root.set("menuKey", json::Value(s.menuKey));
    root.set("unloadKey", json::Value(s.unloadKey));
    root.set("blockGameInputWhileOpen", json::Value(s.blockGameInputWhileOpen));

    root.set("menuOpenOnInject", json::Value(s.menuOpenOnInject));
    root.set("showWatermark", json::Value(s.showWatermark));
    root.set("uiScale", json::Value(static_cast<double>(s.uiScale)));
    root.set("uiAlpha", json::Value(static_cast<double>(s.uiAlpha)));

    json::Value accent = json::Value::Array();
    for (int i = 0; i < kAccentComponents; ++i)
        accent.push(json::Value(static_cast<double>(s.accent[i])));
    root.set("accent", std::move(accent));

    root.set("consoleEnabled", json::Value(s.consoleEnabled));
    root.set("logToFile", json::Value(s.logToFile));
    root.set("showDemoWindow", json::Value(s.showDemoWindow));
}

void ReadSettings(const json::Value& root, Settings& s)
{
    if (const json::Value* v = root.find("menuKey"))
        s.menuKey = v->asInt(s.menuKey);
    if (const json::Value* v = root.find("unloadKey"))
        s.unloadKey = v->asInt(s.unloadKey);
    if (const json::Value* v = root.find("blockGameInputWhileOpen"))
        s.blockGameInputWhileOpen = v->asBool(s.blockGameInputWhileOpen);

    if (const json::Value* v = root.find("menuOpenOnInject"))
        s.menuOpenOnInject = v->asBool(s.menuOpenOnInject);
    if (const json::Value* v = root.find("showWatermark"))
        s.showWatermark = v->asBool(s.showWatermark);
    if (const json::Value* v = root.find("uiScale"))
        s.uiScale = v->asFloat(s.uiScale);
    if (const json::Value* v = root.find("uiAlpha"))
        s.uiAlpha = v->asFloat(s.uiAlpha);
    if (const json::Value* v = root.find("accent")) {
        if (v->isArray() && v->size() == static_cast<std::size_t>(kAccentComponents)) {
            for (int i = 0; i < kAccentComponents; ++i)
                s.accent[i] = v->at(static_cast<std::size_t>(i)).asFloat(s.accent[i]);
        }
    }

    if (const json::Value* v = root.find("consoleEnabled"))
        s.consoleEnabled = v->asBool(s.consoleEnabled);
    if (const json::Value* v = root.find("logToFile"))
        s.logToFile = v->asBool(s.logToFile);
    if (const json::Value* v = root.find("showDemoWindow"))
        s.showDemoWindow = v->asBool(s.showDemoWindow);
}

std::string ReadFile(const std::string& path, bool& ok)
{
    ok = false;
    std::string contents;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
        return contents;

    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        contents.resize(static_cast<std::size_t>(size));
        const std::size_t read = std::fread(contents.data(), 1, contents.size(), file);
        contents.resize(read);
    }
    std::fclose(file);
    ok = true;
    return contents;
}

bool WriteFile(const std::string& path, const std::string& contents)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    const bool ok = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    std::fclose(file);
    return ok;
}

} // namespace

std::string Path()
{
    return paths::ConfigPath();
}

bool Load(Document& doc)
{
    const std::string path = Path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return false;

    bool read = false;
    const std::string text = ReadFile(path, read);
    if (!read) {
        log::Warning("config: could not read %s", path.c_str());
        return false;
    }

    std::string error;
    const json::Value root = json::parse(text, &error);
    if (!root.isObject()) {
        log::Warning("config: %s is not valid json (%s) - keeping defaults",
                     path.c_str(), error.c_str());
        return false;
    }

    ReadSettings(root, doc.settings);
    if (const json::Value* modules = root.find("modules")) {
        if (modules->isObject())
            doc.modules = *modules;
    }
    return true;
}

bool Save(const Document& doc)
{
    const std::string path = Path();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    json::Value root = json::Value::Object();
    WriteSettings(root, doc.settings);
    root.set("modules", doc.modules);

    const std::string text = root.dump(2) + "\n";
    if (!WriteFile(path, text)) {
        log::Warning("config: could not write %s", path.c_str());
        return false;
    }
    return true;
}

bool ResetToDefaults()
{
    std::error_code ec;
    return std::filesystem::remove(Path(), ec);
}

} // namespace bd::config
