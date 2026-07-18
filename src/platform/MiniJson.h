#pragma once
#include <map>
#include <string>
#include <vector>

namespace platform {

// Minimal JSON value model, parser, and serialization helpers shared by
// every hand-rolled schema-specific (de)serializer in this app
// (profiles::ProfileJson, groups::GroupJson, ...). Deliberately NOT a
// general-purpose JSON library -- see profiles/ProfileJson.h for why one
// wasn't vendored: every schema here is a handful of flat/lightly-nested
// fields, not general JSON. No unicode escapes, no floating point (no
// schema needs them).
struct JsonValue {
    enum class Type { Null, Bool, Int, String, Array, Object } type = Type::Null;
    bool boolValue = false;
    long long intValue = 0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;
};

// Parses `text` into `out`. Returns false on any malformed/unexpected
// input -- `out` is left partially populated in that case, callers should
// discard it rather than rely on partial contents.
bool ParseJson(const std::string& text, JsonValue& out);

std::string EscapeJsonString(const std::string& s);

int GetJsonInt(const JsonValue& obj, const char* key, int defaultValue);
bool GetJsonBool(const JsonValue& obj, const char* key, bool defaultValue);
std::string GetJsonString(const JsonValue& obj, const char* key, const std::string& defaultValue);

// Renders a list of already-serialized object text blocks (each one
// "    {\n...\n    }" with no trailing comma) as a JSON array literal --
// shared by every top-level array a schema-specific serializer builds
// (ProfileJson's "profiles"/"rules", GroupJson's "groups" and each
// group's nested "entries").
std::string SerializeJsonArray(const std::vector<std::string>& objectTexts);

} // namespace platform
