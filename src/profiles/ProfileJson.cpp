#include "ProfileJson.h"
#include <cctype>
#include <cstdlib>
#include <map>

namespace profiles {

namespace {

// Minimal JSON value model — just enough to round-trip our fixed schema.
// No unicode escapes, no floating point (none of our fields need it).
struct JsonValue {
    enum class Type { Null, Bool, Int, String, Array, Object } type = Type::Null;
    bool boolValue = false;
    long long intValue = 0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;
};

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
        // Tolerate (but truncate) a fractional part — none of our fields need it.
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ == start) return false;
        out.intValue = std::strtoll(text_.substr(start, pos_ - start).c_str(), nullptr, 10);
        return true;
    }
};

std::string EscapeString(const std::string& s) {
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

int GetInt(const JsonValue& obj, const char* key, int defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::Int) return defaultValue;
    return static_cast<int>(it->second.intValue);
}

bool GetBool(const JsonValue& obj, const char* key, bool defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::Bool) return defaultValue;
    return it->second.boolValue;
}

std::string GetString(const JsonValue& obj, const char* key, const std::string& defaultValue) {
    auto it = obj.objectValue.find(key);
    if (it == obj.objectValue.end() || it->second.type != JsonValue::Type::String) return defaultValue;
    return it->second.stringValue;
}

std::string SerializeProfileObject(const Profile& p) {
    std::string out;
    out += "    {\n";
    out += "      \"id\": \"" + EscapeString(p.id) + "\",\n";
    out += "      \"name\": \"" + EscapeString(p.name) + "\",\n";
    out += "      \"icon\": \"" + EscapeString(p.icon) + "\",\n";
    out += "      \"powerPlan\": " + std::to_string(static_cast<int>(p.vars.powerPlan)) + ",\n";
    out += "      \"screenOffTimeoutAcSec\": " + std::to_string(p.vars.screenOffTimeoutAcSec) + ",\n";
    out += "      \"screenOffTimeoutDcSec\": " + std::to_string(p.vars.screenOffTimeoutDcSec) + ",\n";
    out += "      \"sleepTimeoutAcSec\": " + std::to_string(p.vars.sleepTimeoutAcSec) + ",\n";
    out += "      \"sleepTimeoutDcSec\": " + std::to_string(p.vars.sleepTimeoutDcSec) + ",\n";
    out += "      \"hibernateTimeoutAcSec\": " + std::to_string(p.vars.hibernateTimeoutAcSec) + ",\n";
    out += "      \"hibernateTimeoutDcSec\": " + std::to_string(p.vars.hibernateTimeoutDcSec) + ",\n";
    out += "      \"brightnessPercent\": " + std::to_string(p.vars.brightnessPercent) + ",\n";
    out += std::string("      \"volumeApply\": ") + (p.vars.volumePercent.apply ? "true" : "false") + ",\n";
    out += "      \"volumeValue\": " + std::to_string(p.vars.volumePercent.value) + "\n";
    out += "    }";
    return out;
}

bool ParseProfileObject(const JsonValue& item, Profile& outProfile) {
    if (item.type != JsonValue::Type::Object) return false;

    Profile p;
    p.id = GetString(item, "id", "");
    if (p.id.empty()) return false;
    p.name = GetString(item, "name", "");
    p.icon = GetString(item, "icon", "");
    p.isPredefined = false;
    p.vars.powerPlan =
        static_cast<PowerPlan>(GetInt(item, "powerPlan", static_cast<int>(PowerPlan::Balanced)));
    p.vars.screenOffTimeoutAcSec = GetInt(item, "screenOffTimeoutAcSec", 600);
    p.vars.screenOffTimeoutDcSec = GetInt(item, "screenOffTimeoutDcSec", 300);
    p.vars.sleepTimeoutAcSec = GetInt(item, "sleepTimeoutAcSec", 1800);
    p.vars.sleepTimeoutDcSec = GetInt(item, "sleepTimeoutDcSec", 900);
    p.vars.hibernateTimeoutAcSec = GetInt(item, "hibernateTimeoutAcSec", 0);
    p.vars.hibernateTimeoutDcSec = GetInt(item, "hibernateTimeoutDcSec", 0);
    p.vars.brightnessPercent = GetInt(item, "brightnessPercent", 70);
    p.vars.volumePercent.apply = GetBool(item, "volumeApply", false);
    p.vars.volumePercent.value = GetInt(item, "volumeValue", 0);

    outProfile = std::move(p);
    return true;
}

std::string SerializeRuleObject(const AutomationRule& r) {
    std::string out;
    out += "    {\n";
    out += "      \"id\": \"" + EscapeString(r.id) + "\",\n";
    out += std::string("      \"enabled\": ") + (r.enabled ? "true" : "false") + ",\n";
    out += "      \"type\": " + std::to_string(static_cast<int>(r.type)) + ",\n";
    out += "      \"startMinuteOfDay\": " + std::to_string(r.startMinuteOfDay) + ",\n";
    out += "      \"endMinuteOfDay\": " + std::to_string(r.endMinuteOfDay) + ",\n";
    out += std::string("      \"weekdaysOnly\": ") + (r.weekdaysOnly ? "true" : "false") + ",\n";
    out += "      \"batteryThresholdPercent\": " + std::to_string(r.batteryThresholdPercent) + ",\n";
    out += std::string("      \"triggerOnAcToBattery\": ") + (r.triggerOnAcToBattery ? "true" : "false") + ",\n";
    out += "      \"targetProfileId\": \"" + EscapeString(r.targetProfileId) + "\"\n";
    out += "    }";
    return out;
}

bool ParseRuleObject(const JsonValue& item, AutomationRule& outRule) {
    if (item.type != JsonValue::Type::Object) return false;

    AutomationRule r;
    r.id = GetString(item, "id", "");
    if (r.id.empty()) return false;
    r.enabled = GetBool(item, "enabled", true);
    r.type = static_cast<RuleTriggerType>(GetInt(item, "type", static_cast<int>(RuleTriggerType::TimeWindow)));
    r.startMinuteOfDay = GetInt(item, "startMinuteOfDay", 0);
    r.endMinuteOfDay = GetInt(item, "endMinuteOfDay", 0);
    r.weekdaysOnly = GetBool(item, "weekdaysOnly", false);
    r.batteryThresholdPercent = GetInt(item, "batteryThresholdPercent", 20);
    r.triggerOnAcToBattery = GetBool(item, "triggerOnAcToBattery", true);
    r.targetProfileId = GetString(item, "targetProfileId", "");

    outRule = std::move(r);
    return true;
}

std::string SerializeArray(const std::vector<std::string>& objectTexts) {
    std::string out;
    out += "[\n";
    for (size_t i = 0; i < objectTexts.size(); ++i) {
        out += objectTexts[i];
        out += (i + 1 < objectTexts.size()) ? ",\n" : "\n";
    }
    out += "  ]";
    return out;
}

} // namespace

std::string SerializeProfiles(const std::vector<Profile>& profiles) {
    std::vector<std::string> objectTexts;
    objectTexts.reserve(profiles.size());
    for (const Profile& p : profiles) {
        objectTexts.push_back(SerializeProfileObject(p));
    }
    std::string out;
    out += "{\n  \"version\": 1,\n  \"profiles\": ";
    out += SerializeArray(objectTexts);
    out += "\n}\n";
    return out;
}

bool DeserializeProfiles(const std::string& jsonText, std::vector<Profile>& outProfiles) {
    JsonValue root;
    JsonParser parser(jsonText);
    if (!parser.Parse(root) || root.type != JsonValue::Type::Object) return false;

    auto profilesIt = root.objectValue.find("profiles");
    if (profilesIt == root.objectValue.end() || profilesIt->second.type != JsonValue::Type::Array) {
        return false;
    }

    std::vector<Profile> result;
    for (const JsonValue& item : profilesIt->second.arrayValue) {
        Profile p;
        if (ParseProfileObject(item, p)) {
            result.push_back(std::move(p));
        }
    }

    outProfiles = std::move(result);
    return true;
}

std::string SerializeRules(const std::vector<AutomationRule>& rules) {
    std::vector<std::string> objectTexts;
    objectTexts.reserve(rules.size());
    for (const AutomationRule& r : rules) {
        objectTexts.push_back(SerializeRuleObject(r));
    }
    std::string out;
    out += "{\n  \"version\": 1,\n  \"rules\": ";
    out += SerializeArray(objectTexts);
    out += "\n}\n";
    return out;
}

bool DeserializeRules(const std::string& jsonText, std::vector<AutomationRule>& outRules) {
    JsonValue root;
    JsonParser parser(jsonText);
    if (!parser.Parse(root) || root.type != JsonValue::Type::Object) return false;

    auto rulesIt = root.objectValue.find("rules");
    if (rulesIt == root.objectValue.end() || rulesIt->second.type != JsonValue::Type::Array) {
        // Absent "rules" array is not an error -- older files (or files
        // written before this phase) simply have zero automation rules.
        outRules.clear();
        return true;
    }

    std::vector<AutomationRule> result;
    for (const JsonValue& item : rulesIt->second.arrayValue) {
        AutomationRule r;
        if (ParseRuleObject(item, r)) {
            result.push_back(std::move(r));
        }
    }

    outRules = std::move(result);
    return true;
}

std::string SerializeDocument(const std::vector<Profile>& profiles, const std::vector<AutomationRule>& rules) {
    std::vector<std::string> profileTexts;
    profileTexts.reserve(profiles.size());
    for (const Profile& p : profiles) {
        profileTexts.push_back(SerializeProfileObject(p));
    }

    std::vector<std::string> ruleTexts;
    ruleTexts.reserve(rules.size());
    for (const AutomationRule& r : rules) {
        ruleTexts.push_back(SerializeRuleObject(r));
    }

    std::string out;
    out += "{\n  \"version\": 1,\n  \"profiles\": ";
    out += SerializeArray(profileTexts);
    out += ",\n  \"rules\": ";
    out += SerializeArray(ruleTexts);
    out += "\n}\n";
    return out;
}

} // namespace profiles
