// Standalone unit tests for the hand-rolled JSON module.
// It is deliberately free of Windows headers so you can build it anywhere:
//
//   cl /std:c++20 /EHsc tests/json_tests.cpp src/core/util/json.cpp && json_tests.exe
//   g++ -std=c++20 tests/json_tests.cpp src/core/util/json.cpp -o json_tests && ./json_tests
//
// It is not part of the Visual Studio solution (the DLL never runs it).

#include "../src/core/util/json.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const char* what)
{
    if (condition) {
        std::printf("  ok    %s\n", what);
    } else {
        std::printf("  FAIL  %s\n", what);
        ++g_failures;
    }
}

void CheckString(const std::string& actual, const std::string& expected, const char* what)
{
    Check(actual == expected, what);
    if (actual != expected)
        std::printf("        expected: %s\n        actual:   %s\n", expected.c_str(), actual.c_str());
}

void TestRoundTrip()
{
    std::printf("round trip\n");

    bd::json::Value root = bd::json::Value::Object();
    root.set("title", bd::json::Value("Banana Drama"));
    root.set("menuKey", bd::json::Value(45));
    root.set("uiScale", bd::json::Value(1.25));
    root.set("enabled", bd::json::Value(true));
    root.set("missing", bd::json::Value());

    bd::json::Value nested = bd::json::Value::Object();
    nested.set("corner", bd::json::Value(3));
    root.set("overlay", std::move(nested));

    bd::json::Value list = bd::json::Value::Array();
    list.push(bd::json::Value(1));
    list.push(bd::json::Value(2.5));
    list.push(bd::json::Value("three"));
    root.set("list", std::move(list));

    const std::string text = root.dump(2);
    std::string error;
    const bd::json::Value parsed = bd::json::parse(text, &error);

    Check(error.empty(), "parses without error");
    Check(parsed.isObject(), "root is an object");
    CheckString(parsed.find("title")->asString(), "Banana Drama", "string round trips");
    Check(parsed.find("menuKey")->asInt() == 45, "int round trips");
    Check(std::fabs(parsed.find("uiScale")->asNumber() - 1.25) < 1e-9, "double round trips");
    Check(parsed.find("enabled")->asBool() == true, "bool round trips");
    Check(parsed.find("missing")->isNull(), "null round trips");
    Check(parsed.find("overlay")->find("corner")->asInt() == 3, "nested object round trips");
    Check(parsed.find("list")->size() == 3, "array size round trips");
    Check(parsed.find("list")->at(2).asString() == "three", "array element round trips");
    Check(parsed.find("list")->at(1).asFloat() == 2.5f, "array float round trips");
    Check(parsed.find("nope") == nullptr, "missing key returns nullptr");
    Check(parsed.find("title")->asInt(7) == 7, "type mismatch falls back");
}

void TestEscapes()
{
    std::printf("escapes\n");

    bd::json::Value root = bd::json::Value::Object();
    root.set("quote", bd::json::Value(std::string("say \"hi\"")));
    root.set("path", bd::json::Value(std::string("C:\\Games\\Banana")));
    root.set("newline", bd::json::Value(std::string("a\nb\tc")));
    root.set("unicode", bd::json::Value(std::string("\xE2\x80\x94"))); // em dash (UTF-8)

    const std::string text = root.dump(0);
    std::string error;
    const bd::json::Value parsed = bd::json::parse(text, &error);

    Check(error.empty(), "escaped document parses");
    CheckString(parsed.find("quote")->asString(), "say \"hi\"", "quote escapes");
    CheckString(parsed.find("path")->asString(), "C:\\Games\\Banana", "backslash escapes");
    CheckString(parsed.find("newline")->asString(), "a\nb\tc", "control escapes");
    CheckString(parsed.find("unicode")->asString(), "\xE2\x80\x94", "utf-8 passes through");
}

void TestMalformed()
{
    std::printf("malformed input\n");

    const char* bad[] = {
        "",
        "{",
        "{\"a\"}",
        "{\"a\":}",
        "[1,2",
        "{\"a\":01x}",
        "\"unterminated",
        "{\"a\": tru}",
        "{,}",
    };

    for (const char* text : bad) {
        std::string error;
        const bd::json::Value value = bd::json::parse(text, &error);
        const bool rejected = value.isNull() && !error.empty();
        Check(rejected, text);
    }
}

void TestRealistic()
{
    std::printf("realistic settings file\n");

    const std::string text =
        "{\n"
        "  \"input\": { \"menuKey\": 45, \"unloadKey\": 35 },\n"
        "  \"appearance\": { \"uiScale\": 1.5, \"accent\": [0.98, 0.78, 0.13, 1] },\n"
        "  \"modules\": { \"overlay\": { \"enabled\": true, \"showFps\": false } }\n"
        "}\n";

    std::string error;
    const bd::json::Value root = bd::json::parse(text, &error);

    Check(error.empty(), "parses");
    Check(root.find("input")->find("menuKey")->asInt() == 45, "nested int");
    Check(root.find("appearance")->find("accent")->at(1).asFloat() == 0.78f, "colour array");
    Check(root.find("modules")->find("overlay")->find("showFps")->asBool(false) == false, "module flag");
    Check(root.find("modules")->find("sandbox") == nullptr, "absent module");
}

} // namespace

int main()
{
    TestRoundTrip();
    TestEscapes();
    TestMalformed();
    TestRealistic();

    if (g_failures == 0) {
        std::printf("\nall tests passed\n");
        return 0;
    }
    std::printf("\n%d test(s) failed\n", g_failures);
    return 1;
}
