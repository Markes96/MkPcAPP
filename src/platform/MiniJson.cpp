#include "MiniJson.h"
#include <cctype>
#include <cstdlib>

namespace platform {

namespace {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& out) {
        SkipWhitespace();
        return ParseValue(out);
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    char Peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char Next() { return pos_ < text_.size() ? text_[pos_++] : '\0'; }
    void SkipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool ParseValue(JsonValue& out) {
        SkipWhitespace();
        char c = Peek();
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') return ParseString(out);
        if (c == 't' || c == 'f') return ParseBool(out);
        if (c == 'n') return ParseNull(out);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ParseNumber(out);
        return false;
    }

    bool ParseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        ++pos_; // consume '{'
        SkipWhitespace();
        if (Peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            SkipWhitespace();
            JsonValue key;
            if (Peek() != '"' || !ParseString(key)) return false;
            SkipWhitespace();
            if (Next() != ':') return false;
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.objectValue[key.stringValue] = std::move(value);
            SkipWhitespace();
            char c = Next();
            if (c == ',') continue;
            if (c == '}') break;
            return false;
        }
        return true;
    }

    bool ParseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        ++pos_; // consume '['
        SkipWhitespace();
        if (Peek() == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            JsonValue value;
            if (!ParseValue(value)) return false;
            out.arrayValue.push_back(std::move(value));
            SkipWhitespace();
            char c = Next();
            if (c == ',') continue;
            if (c == ']') break;
            return false;
        }
        return true;
    }

    bool ParseString(JsonValue& out) {
        out.type = JsonValue::Type::String;
        if (Next() != '"') return false;
        std::string result;
        while (true) {
            if (pos_ >= text_.size()) return false;
            char c = Next();
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) return false;
                char esc = Next();
                switch (esc) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    case 'r': result += '\r'; break;
                    default: result += esc; break;
                }
            } else {
                result += c;
            }
        }
        out.stringValue = std::move(result);
        return true;
    }

    bool ParseBool(JsonValue& out) {
        out.type = JsonValue::Type::Bool;
        if (text_.compare(pos_, 4, "true") == 0) {
            out.boolValue = true;
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            out.boolValue = false;
            pos_ += 5;
            return true;
        }
        return false;
    }

    bool ParseNull(JsonValue& out) {
        out.type = JsonValue::Type::Null;
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return true;
        }
        return false;
    }

    bool ParseNumber(JsonValue& out) {
        out.type = JsonValue::Type::Int;
        size_t start = pos_;
        if (Peek() == '-') ++pos_;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        // Tolerate (but truncate) a fractional part -- none of our fields need it.
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ == start) return false;
        out.intValue = std::strtoll(text_.substr(start, pos_ - start).c_str(), nullptr, 10);
        return true;
    }
};

} // namespace

bool ParseJson(const std::string& text, JsonValue& out) {
    JsonParser parser(text);
    return parser.Parse(out);
}

std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

int GetJsonInt(const JsonValue& obj, const char* key, int defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::Int) return defaultValue;
    return static_cast<int>(it->second.intValue);
}

bool GetJsonBool(const JsonValue& obj, const char* key, bool defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::Bool) return defaultValue;
    return it->second.boolValue;
}

std::string GetJsonString(const JsonValue& obj, const char* key, const std::string& defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::String) return defaultValue;
    return it->second.stringValue;
}

std::string SerializeJsonArray(const std::vector<std::string>& objectTexts) {
    std::string out;
    out += "[\n";
    for (size_t i = 0; i < objectTexts.size(); ++i) {
        out += objectTexts[i];
        out += (i + 1 < objectTexts.size()) ? ",\n" : "\n";
    }
    out += "  ]";
    return out;
}

} // namespace platform
