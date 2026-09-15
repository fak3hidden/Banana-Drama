#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bd::json {

// ---------------------------------------------------------------- constructors

Value::Value(bool v) : type_(Type::Bool), bool_(v) {}
Value::Value(int v) : type_(Type::Number), number_(static_cast<double>(v)) {}
Value::Value(double v) : type_(Type::Number), number_(v) {}
Value::Value(const char* v) : type_(Type::String), string_(v ? v : "") {}
Value::Value(std::string v) : type_(Type::String), string_(std::move(v)) {}

Value Value::Object()
{
    Value v;
    v.type_ = Type::Object;
    return v;
}

Value Value::Array()
{
    Value v;
    v.type_ = Type::Array;
    return v;
}

// -------------------------------------------------------------------- objects

void Value::set(const std::string& key, Value v)
{
    type_ = Type::Object;
    for (auto& member : members_) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    members_.emplace_back(key, std::move(v));
}

const Value* Value::find(const std::string& key) const
{
    if (type_ != Type::Object)
        return nullptr;
    for (const auto& member : members_) {
        if (member.first == key)
            return &member.second;
    }
    return nullptr;
}

// --------------------------------------------------------------------- arrays

void Value::push(Value v)
{
    type_ = Type::Array;
    array_.push_back(std::move(v));
}

std::size_t Value::size() const
{
    return type_ == Type::Array ? array_.size() : (type_ == Type::Object ? members_.size() : 0u);
}

const Value& Value::at(std::size_t index) const
{
    static const Value nullValue;
    if (type_ != Type::Array || index >= array_.size())
        return nullValue;
    return array_[index];
}

// -------------------------------------------------------------------- getters

bool Value::asBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }

int Value::asInt(int fallback) const
{
    return type_ == Type::Number ? static_cast<int>(number_) : fallback;
}

double Value::asNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

float Value::asFloat(float fallback) const
{
    return type_ == Type::Number ? static_cast<float>(number_) : fallback;
}

std::string Value::asString(const std::string& fallback) const
{
    return type_ == Type::String ? string_ : fallback;
}

// -------------------------------------------------------------------- dumping

namespace {

void AppendEscaped(std::string& out, const std::string& text)
{
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

void AppendNumber(std::string& out, double value)
{
    char buf[48];
    double integral = 0.0;
    if (std::modf(value, &integral) == 0.0 && value >= -9.0e15 && value <= 9.0e15)
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    else
        std::snprintf(buf, sizeof(buf), "%.10g", value);
    out += buf;
}

} // namespace

void Value::dumpInto(std::string& out, int indent, int depth) const
{
    const std::string padInner(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth + 1), ' ');
    const std::string padOuter(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');

    switch (type_) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += bool_ ? "true" : "false";
        break;
    case Type::Number:
        AppendNumber(out, number_);
        break;
    case Type::String:
        AppendEscaped(out, string_);
        break;
    case Type::Array:
        if (array_.empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (std::size_t i = 0; i < array_.size(); ++i) {
            out += padInner;
            array_[i].dumpInto(out, indent, depth + 1);
            if (i + 1 < array_.size())
                out += ',';
            out += '\n';
        }
        out += padOuter;
        out += ']';
        break;
    case Type::Object:
        if (members_.empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        for (std::size_t i = 0; i < members_.size(); ++i) {
            out += padInner;
            AppendEscaped(out, members_[i].first);
            out += ": ";
            members_[i].second.dumpInto(out, indent, depth + 1);
            if (i + 1 < members_.size())
                out += ',';
            out += '\n';
        }
        out += padOuter;
        out += '}';
        break;
    }
}

std::string Value::dump(int indent) const
{
    std::string out;
    dumpInto(out, indent, 0);
    return out;
}

// -------------------------------------------------------------------- parsing

namespace {

int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool Run(Value& out, std::string& error)
    {
        SkipWhitespace();
        if (!ParseValue(out)) {
            error = message_;
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;
    bool failed_ = false;
    std::string message_;

    bool Fail(const char* message)
    {
        if (!failed_) {
            failed_ = true;
            message_ = std::string(message) + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    bool AtEnd() const { return pos_ >= text_.size(); }
    char Cur() const { return text_[pos_]; }

    void SkipWhitespace()
    {
        while (!AtEnd()) {
            const char c = Cur();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool Peek(char c) const { return !AtEnd() && Cur() == c; }

    bool Expect(char c)
    {
        if (!Peek(c))
            return Fail("unexpected character");
        ++pos_;
        return true;
    }

    bool ParseValue(Value& out)
    {
        if (AtEnd())
            return Fail("unexpected end of input");

        switch (Cur()) {
        case '{': return ParseObject(out);
        case '[': return ParseArray(out);
        case '"': {
            std::string text;
            if (!ParseString(text))
                return false;
            out = Value(std::move(text));
            return true;
        }
        case 't': return ParseLiteral("true", Value(true), out);
        case 'f': return ParseLiteral("false", Value(false), out);
        case 'n': return ParseLiteral("null", Value(), out);
        default:  return ParseNumber(out);
        }
    }

    bool ParseLiteral(const char* literal, Value value, Value& out)
    {
        const std::size_t length = std::strlen(literal);
        if (text_.compare(pos_, length, literal) != 0)
            return Fail("invalid literal");
        pos_ += length;
        out = std::move(value);
        return true;
    }

    bool ParseObject(Value& out)
    {
        ++pos_; // '{'
        Value object = Value::Object();
        SkipWhitespace();
        if (Peek('}')) {
            ++pos_;
            out = std::move(object);
            return true;
        }
        for (;;) {
            SkipWhitespace();
            if (!Peek('"'))
                return Fail("expected object key");
            std::string key;
            if (!ParseString(key))
                return false;
            SkipWhitespace();
            if (!Expect(':'))
                return false;
            SkipWhitespace();
            Value member;
            if (!ParseValue(member))
                return false;
            object.set(key, std::move(member));
            SkipWhitespace();
            if (Peek(',')) {
                ++pos_;
                continue;
            }
            if (!Expect('}'))
                return false;
            out = std::move(object);
            return true;
        }
    }

    bool ParseArray(Value& out)
    {
        ++pos_; // '['
        Value array = Value::Array();
        SkipWhitespace();
        if (Peek(']')) {
            ++pos_;
            out = std::move(array);
            return true;
        }
        for (;;) {
            SkipWhitespace();
            Value element;
            if (!ParseValue(element))
                return false;
            array.push(std::move(element));
            SkipWhitespace();
            if (Peek(',')) {
                ++pos_;
                continue;
            }
            if (!Expect(']'))
                return false;
            out = std::move(array);
            return true;
        }
    }

    bool ParseString(std::string& out)
    {
        ++pos_; // opening quote
        std::string text;
        while (!AtEnd()) {
            const char c = text_[pos_++];
            if (c == '"') {
                out = std::move(text);
                return true;
            }
            if (c != '\\') {
                text += c;
                continue;
            }
            if (AtEnd())
                return Fail("unterminated escape sequence");
            const char escape = text_[pos_++];
            switch (escape) {
            case '"':  text += '"';  break;
            case '\\': text += '\\'; break;
            case '/':  text += '/';  break;
            case 'b':  text += '\b'; break;
            case 'f':  text += '\f'; break;
            case 'n':  text += '\n'; break;
            case 'r':  text += '\r'; break;
            case 't':  text += '\t'; break;
            case 'u': {
                if (pos_ + 4 > text_.size())
                    return Fail("truncated unicode escape");
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const int digit = HexDigit(text_[pos_++]);
                    if (digit < 0)
                        return Fail("invalid hex digit");
                    code = code * 16u + static_cast<unsigned>(digit);
                }
                if (code < 0x80) {
                    text += static_cast<char>(code);
                } else if (code < 0x800) {
                    text += static_cast<char>(0xC0u | (code >> 6));
                    text += static_cast<char>(0x80u | (code & 0x3Fu));
                } else {
                    text += static_cast<char>(0xE0u | (code >> 12));
                    text += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
                    text += static_cast<char>(0x80u | (code & 0x3Fu));
                }
                break;
            }
            default:
                return Fail("unknown escape sequence");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(Value& out)
    {
        const std::size_t start = pos_;
        if (Peek('-') || Peek('+'))
            ++pos_;
        bool digits = false;
        while (!AtEnd()) {
            const char c = Cur();
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits = true;
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E') {
                ++pos_;
            } else if ((c == '-' || c == '+') && (text_[pos_ - 1] == 'e' || text_[pos_ - 1] == 'E')) {
                ++pos_;
            } else {
                break;
            }
        }
        if (!digits)
            return Fail("invalid number");
        const std::string slice = text_.substr(start, pos_ - start);
        out = Value(std::strtod(slice.c_str(), nullptr));
        return true;
    }
};

} // namespace

Value parse(const std::string& text, std::string* error)
{
    Parser parser(text);
    Value result;
    std::string message;
    if (!parser.Run(result, message)) {
        if (error)
            *error = message;
        return Value();
    }
    return result;
}

} // namespace bd::json
