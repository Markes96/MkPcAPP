#include "ProfileJson.h"
#include "../platform/MiniJson.h"

namespace profiles {

namespace {

using platform::EscapeJsonString;
using platform::GetJsonBool;
using platform::GetJsonInt;
using platform::GetJsonString;
using platform::JsonValue;
using platform::SerializeJsonArray;

std::string SerializeProfileObject(const Profile& p) {
    std::string out;
    out += "    {\n";
    out += "      \"id\": \"" + EscapeJsonString(p.id) + "\",\n";
    out += "      \"name\": \"" + EscapeJsonString(p.name) + "\",\n";
    out += "      \"icon\": \"" + EscapeJsonString(p.icon) + "\",\n";
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
    p.id = GetJsonString(item, "id", "");
    if (p.id.empty()) return false;
    p.name = GetJsonString(item, "name", "");
    p.icon = GetJsonString(item, "icon", "");
    p.isPredefined = false;
    p.vars.powerPlan =
        static_cast<PowerPlan>(GetJsonInt(item, "powerPlan", static_cast<int>(PowerPlan::Balanced)));
    p.vars.screenOffTimeoutAcSec = GetJsonInt(item, "screenOffTimeoutAcSec", 600);
    p.vars.screenOffTimeoutDcSec = GetJsonInt(item, "screenOffTimeoutDcSec", 300);
    p.vars.sleepTimeoutAcSec = GetJsonInt(item, "sleepTimeoutAcSec", 1800);
    p.vars.sleepTimeoutDcSec = GetJsonInt(item, "sleepTimeoutDcSec", 900);
    p.vars.hibernateTimeoutAcSec = GetJsonInt(item, "hibernateTimeoutAcSec", 0);
    p.vars.hibernateTimeoutDcSec = GetJsonInt(item, "hibernateTimeoutDcSec", 0);
    p.vars.brightnessPercent = GetJsonInt(item, "brightnessPercent", 70);
    p.vars.volumePercent.apply = GetJsonBool(item, "volumeApply", false);
    p.vars.volumePercent.value = GetJsonInt(item, "volumeValue", 0);

    outProfile = std::move(p);
    return true;
}

std::string SerializeRuleObject(const AutomationRule& r) {
    std::string out;
    out += "    {\n";
    out += "      \"id\": \"" + EscapeJsonString(r.id) + "\",\n";
    out += std::string("      \"enabled\": ") + (r.enabled ? "true" : "false") + ",\n";
    out += "      \"type\": " + std::to_string(static_cast<int>(r.type)) + ",\n";
    out += "      \"startMinuteOfDay\": " + std::to_string(r.startMinuteOfDay) + ",\n";
    out += "      \"endMinuteOfDay\": " + std::to_string(r.endMinuteOfDay) + ",\n";
    out += std::string("      \"weekdaysOnly\": ") + (r.weekdaysOnly ? "true" : "false") + ",\n";
    out += "      \"batteryThresholdPercent\": " + std::to_string(r.batteryThresholdPercent) + ",\n";
    out += std::string("      \"triggerOnAcToBattery\": ") + (r.triggerOnAcToBattery ? "true" : "false") + ",\n";
    out += "      \"targetProfileId\": \"" + EscapeJsonString(r.targetProfileId) + "\"\n";
    out += "    }";
    return out;
}

bool ParseRuleObject(const JsonValue& item, AutomationRule& outRule) {
    if (item.type != JsonValue::Type::Object) return false;

    AutomationRule r;
    r.id = GetJsonString(item, "id", "");
    if (r.id.empty()) return false;
    r.enabled = GetJsonBool(item, "enabled", true);
    r.type = static_cast<RuleTriggerType>(GetJsonInt(item, "type", static_cast<int>(RuleTriggerType::TimeWindow)));
    r.startMinuteOfDay = GetJsonInt(item, "startMinuteOfDay", 0);
    r.endMinuteOfDay = GetJsonInt(item, "endMinuteOfDay", 0);
    r.weekdaysOnly = GetJsonBool(item, "weekdaysOnly", false);
    r.batteryThresholdPercent = GetJsonInt(item, "batteryThresholdPercent", 20);
    r.triggerOnAcToBattery = GetJsonBool(item, "triggerOnAcToBattery", true);
    r.targetProfileId = GetJsonString(item, "targetProfileId", "");

    outRule = std::move(r);
    return true;
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
    out += SerializeJsonArray(objectTexts);
    out += "\n}\n";
    return out;
}

bool DeserializeProfiles(const std::string& jsonText, std::vector<Profile>& outProfiles) {
    JsonValue root;
    if (!platform::ParseJson(jsonText, root) || root.type != JsonValue::Type::Object) return false;

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
    out += SerializeJsonArray(objectTexts);
    out += "\n}\n";
    return out;
}

bool DeserializeRules(const std::string& jsonText, std::vector<AutomationRule>& outRules) {
    JsonValue root;
    if (!platform::ParseJson(jsonText, root) || root.type != JsonValue::Type::Object) return false;

    auto rulesIt = root.objectValue.find("rules");
    if (rulesIt == root.objectValue.end() || rulesIt->second.type != JsonValue::Type::Array) {
        // Absent "rules" array is not an error -- older files (or files
        // written before this phase existed) simply have zero automation rules.
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
    out += SerializeJsonArray(profileTexts);
    out += ",\n  \"rules\": ";
    out += SerializeJsonArray(ruleTexts);
    out += "\n}\n";
    return out;
}

} // namespace profiles
