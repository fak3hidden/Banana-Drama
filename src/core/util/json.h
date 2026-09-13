// Minimal JSON value + parser, just enough for a settings file.
// No third-party dependency, no platform headers: this file is plain C++17.
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace bd::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    using Member = std::pair<std::string, Value>;

    Value() = default;
    Value(bool v);
    Value(int v);
    Value(double v);
    Value(const char* v);
    Value(std::string v);

    static Value Object();
    static Value Array();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Object access. set() overwrites an existing member with the same key.
    void set(const std::string& key, Value v);
    const Value* find(const std::string& key) const;

    // Array access.
    void push(Value v);
    std::size_t size() const;
    const Value& at(std::size_t index) const;

    // Typed getters. Each returns the fallback when the value holds another type.
    bool asBool(bool fallback = false) const;
    int asInt(int fallback = 0) const;
    double asNumber(double fallback = 0.0) const;
    float asFloat(float fallback = 0.0f) const;
    std::string asString(const std::string& fallback = std::string()) const;

    // Pretty prints with `indent` spaces per level (0 = compact).
    std::string dump(int indent = 2) const;

private:
    void dumpInto(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<Member> members_;
};

// Parses `text`. On failure returns a null Value and fills `error` when provided.
Value parse(const std::string& text, std::string* error = nullptr);

} // namespace bd::json
