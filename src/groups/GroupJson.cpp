#include "GroupJson.h"
#include "../platform/MiniJson.h"

namespace groups {

namespace {

using platform::EscapeJsonString;
using platform::GetJsonString;
using platform::JsonValue;
using platform::SerializeJsonArray;

std::string SerializeEntryObject(const LaunchEntry& entry) {
    std::string out;
    out += "      {\n";
    out += "        \"path\": \"" + EscapeJsonString(entry.path) + "\",\n";
    out += "        \"resolvedExePath\": \"" + EscapeJsonString(entry.resolvedExePath) + "\",\n";
    out += "        \"args\": \"" + EscapeJsonString(entry.args) + "\"\n";
    out += "      }";
    return out;
}

bool ParseEntryObject(const JsonValue& item, LaunchEntry& outEntry) {
    if (item.type != JsonValue::Type::Object) return false;

    LaunchEntry entry;
    entry.path = GetJsonString(item, "path", "");
    if (entry.path.empty()) return false;
    entry.resolvedExePath = GetJsonString(item, "resolvedExePath", "");
    entry.args = GetJsonString(item, "args", "");

    outEntry = std::move(entry);
    return true;
}

std::string SerializeGroupObject(const LaunchGroup& group) {
    std::vector<std::string> entryTexts;
    entryTexts.reserve(group.entries.size());
    for (const LaunchEntry& entry : group.entries) {
        entryTexts.push_back(SerializeEntryObject(entry));
    }

    std::string out;
    out += "    {\n";
    out += "      \"id\": \"" + EscapeJsonString(group.id) + "\",\n";
    out += "      \"name\": \"" + EscapeJsonString(group.name) + "\",\n";
    out += "      \"entries\": " + SerializeJsonArray(entryTexts) + "\n";
    out += "    }";
    return out;
}

bool ParseGroupObject(const JsonValue& item, LaunchGroup& outGroup) {
    if (item.type != JsonValue::Type::Object) return false;

    LaunchGroup group;
    group.id = GetJsonString(item, "id", "");
    if (group.id.empty()) return false;
    group.name = GetJsonString(item, "name", "");

    auto entriesIt = item.objectValue.find("entries");
    if (entriesIt != item.objectValue.end() && entriesIt->second.type == JsonValue::Type::Array) {
        for (const JsonValue& entryItem : entriesIt->second.arrayValue) {
            LaunchEntry entry;
            if (ParseEntryObject(entryItem, entry)) {
                group.entries.push_back(std::move(entry));
            }
        }
    }

    outGroup = std::move(group);
    return true;
}

} // namespace

std::string SerializeGroups(const std::vector<LaunchGroup>& groups) {
    std::vector<std::string> objectTexts;
    objectTexts.reserve(groups.size());
    for (const LaunchGroup& group : groups) {
        objectTexts.push_back(SerializeGroupObject(group));
    }
    std::string out;
    out += "{\n  \"version\": 1,\n  \"groups\": ";
    out += SerializeJsonArray(objectTexts);
    out += "\n}\n";
    return out;
}

bool DeserializeGroups(const std::string& jsonText, std::vector<LaunchGroup>& outGroups) {
    JsonValue root;
    if (!platform::ParseJson(jsonText, root) || root.type != JsonValue::Type::Object) return false;

    auto groupsIt = root.objectValue.find("groups");
    if (groupsIt == root.objectValue.end() || groupsIt->second.type != JsonValue::Type::Array) {
        return false;
    }

    std::vector<LaunchGroup> result;
    for (const JsonValue& item : groupsIt->second.arrayValue) {
        LaunchGroup group;
        if (ParseGroupObject(item, group)) {
            result.push_back(std::move(group));
        }
    }

    outGroups = std::move(result);
    return true;
}

} // namespace groups
