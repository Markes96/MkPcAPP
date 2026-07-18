# Grupos (abrir/cerrar conjuntos de apps) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new "Grupos" tab to MkPCApp where the user defines named groups of apps (e.g. "League of Legends" = the game + Discord + an overlay) and opens/closes the whole group with one click each.

**Architecture:** New `src/groups/` module (types, JSON persistence, in-memory CRUD manager, process launch/close orchestration with cross-group reference counting) plus a new `ui::GroupsTab` built on the existing `ui::ITab` extension point, following the same file-per-responsibility split as `src/profiles/` and `src/startup/`. A new `platform::MiniJson` module is extracted first so the hand-rolled JSON parser isn't duplicated between `ProfileJson` and the new `GroupJson`.

**Tech Stack:** C++20, Win32 API (CreateProcess, EnumWindows/WM_CLOSE, TerminateProcess, CreateToolhelp32Snapshot), Dear ImGui, CMake/MSVC. No new third-party dependencies.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-07-18-grupos-lanzamiento-design.md` (read it before starting — this plan implements it, including the shared-ownership correction near the top).
- Groups persist to `%LOCALAPPDATA%\MkPCApp\groups.json` — a file separate from `profiles.json`, own schema (`{"version":1,"groups":[{"id","name","entries":[{"path","resolvedExePath","args"}]}]}`).
- Two buttons only, both manual: "Abrir grupo" / "Cerrar grupo". No auto-detection of the game closing.
- The file picker for group entries accepts **any** file (no `.exe`-only filter) — some launch targets (e.g. Minecraft via Java) aren't a bare `.exe`. `.lnk` targets are resolved to their real exe.
- All entries in a group launch together (no per-entry delay).
- Cross-group reference counting: an app only closes when **no currently-open group** still owns it. An app already running before "Abrir grupo" was clicked (for any reason the tracker doesn't recognize — normally the user opened it by hand) is never touched by any group's "Cerrar grupo".
- Close sequence: `WM_CLOSE` to the process's top-level windows, wait ~5 ticks (~5s at the app's 1 Hz tick rate), then `TerminateProcess` if still alive.
- New sidebar tab "Grupos" (`ui::ITab`), registered in `Application::Init` alongside `HardwareMonitorTab`/`PerfilesTab`/`StartupTab` — no other app-shell code changes.
- **This repo has no automated test framework** (confirmed: no test target in `CMakeLists.txt`, no `*test*` files outside `third_party/`) and every existing design spec's own "Verificación" section documents the project's real practice: manual code review plus building and running the real app on Windows. This plan follows that same convention rather than introducing new test infrastructure — do not add a unit-test framework as part of this work. Each task's verification step is "the project builds with zero errors" (`cmake --build build --target MkPCApp --config Debug`, run from the repo root), which is a strong, genuinely automatable signal in a strongly-typed C++ codebase. The final task adds a documented manual QA checklist for the parts that can only be verified by clicking through the real GUI.

---

### Task 1: Extract shared `platform::MiniJson` from `ProfileJson`

**Files:**
- Create: `src/platform/MiniJson.h`
- Create: `src/platform/MiniJson.cpp`
- Modify: `src/profiles/ProfileJson.cpp` (full rewrite of its private JSON machinery to call into `platform::MiniJson` instead)
- Modify: `CMakeLists.txt:47` (add `src/platform/MiniJson.cpp` to `APP_SOURCES`, right before `src/profiles/ProfileJson.cpp`)

**Interfaces:**
- Produces (used by Task 2 and by the refactored `ProfileJson.cpp`):
  - `struct platform::JsonValue { enum class Type { Null, Bool, Int, String, Array, Object } type; bool boolValue; long long intValue; std::string stringValue; std::vector<JsonValue> arrayValue; std::map<std::string, JsonValue> objectValue; };`
  - `bool platform::ParseJson(const std::string& text, JsonValue& out);`
  - `std::string platform::EscapeJsonString(const std::string& s);`
  - `int platform::GetJsonInt(const JsonValue& obj, const char* key, int defaultValue);`
  - `bool platform::GetJsonBool(const JsonValue& obj, const char* key, bool defaultValue);`
  - `std::string platform::GetJsonString(const JsonValue& obj, const char* key, const std::string& defaultValue);`
  - `std::string platform::SerializeJsonArray(const std::vector<std::string>& objectTexts);`

- [ ] **Step 1: Create `src/platform/MiniJson.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/platform/MiniJson.cpp`**

```cpp
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
```

- [ ] **Step 3: Rewrite `src/profiles/ProfileJson.cpp` to use `platform::MiniJson`**

Replace the entire file contents with:

```cpp
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
```

`ProfileJson.h`'s public API (`SerializeProfiles`/`DeserializeProfiles`/`SerializeRules`/`DeserializeRules`/`SerializeDocument`) is untouched, so `ProfileStore.cpp` needs no changes.

- [ ] **Step 4: Add the new source file to `CMakeLists.txt`**

In `CMakeLists.txt`, change:

```cmake
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayIcon.cpp
    src/app/BridgeProcess.cpp
    src/app/AutoStart.cpp
    src/sensors/NativeSensors.cpp
    src/profiles/ProfileJson.cpp
```

to:

```cmake
set(APP_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/app/TrayIcon.cpp
    src/app/BridgeProcess.cpp
    src/app/AutoStart.cpp
    src/sensors/NativeSensors.cpp
    src/platform/MiniJson.cpp
    src/profiles/ProfileJson.cpp
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors. This is a behavior-preserving refactor (profiles.json's on-disk format is byte-for-byte identical — same key order, same escaping, same array rendering), so no `profiles.json` migration is needed; existing custom profiles/rules on disk keep loading exactly as before.

- [ ] **Step 6: Commit**

```bash
git add src/platform/MiniJson.h src/platform/MiniJson.cpp src/profiles/ProfileJson.cpp CMakeLists.txt
git commit -m "Extract shared MiniJson parser from ProfileJson"
```

---

### Task 2: `groups::GroupTypes` + `groups::GroupJson`

**Files:**
- Create: `src/groups/GroupTypes.h`
- Create: `src/groups/GroupJson.h`
- Create: `src/groups/GroupJson.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/GroupJson.cpp`)

**Interfaces:**
- Consumes: `platform::JsonValue`, `platform::ParseJson`, `platform::EscapeJsonString`, `platform::GetJsonString`, `platform::SerializeJsonArray` (Task 1).
- Produces (used by Task 3 `GroupStore` and every later `groups::*` file):
  - `struct groups::LaunchEntry { std::string path; std::string resolvedExePath; std::string args; };`
  - `struct groups::LaunchGroup { std::string id; std::string name; std::vector<LaunchEntry> entries; };`
  - `std::string groups::SerializeGroups(const std::vector<LaunchGroup>& groups);`
  - `bool groups::DeserializeGroups(const std::string& jsonText, std::vector<LaunchGroup>& outGroups);`

- [ ] **Step 1: Create `src/groups/GroupTypes.h`**

```cpp
#pragma once
#include <string>
#include <vector>

namespace groups {

struct LaunchEntry {
    // Path as the user picked it via the file dialog -- a .exe, a .lnk
    // shortcut, or any other executable format (e.g. a .bat, or a Java
    // launcher's own wrapper -- see GroupEditorDialog, which doesn't
    // filter by extension since cases like Minecraft don't launch via a
    // bare .exe).
    std::string path;
    // The exe that will actually run: equal to `path` unless `path` is a
    // .lnk, in which case this is its resolved target. Empty if a .lnk
    // couldn't be resolved (broken shortcut) -- GroupLauncher skips such
    // an entry with an error rather than trying to launch an empty path.
    std::string resolvedExePath;
    // Optional command-line arguments, "" by default.
    std::string args;
};

struct LaunchGroup {
    std::string id;
    std::string name;
    std::vector<LaunchEntry> entries;
};

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/GroupJson.h`**

```cpp
#pragma once
#include "GroupTypes.h"
#include <string>
#include <vector>

namespace groups {

std::string SerializeGroups(const std::vector<LaunchGroup>& groups);

// Parses `jsonText` and replaces outGroups with the parsed list. Returns
// false on any malformed/unexpected input (outGroups left untouched) --
// callers should treat that as "no groups" rather than crash.
bool DeserializeGroups(const std::string& jsonText, std::vector<LaunchGroup>& outGroups);

} // namespace groups
```

- [ ] **Step 3: Create `src/groups/GroupJson.cpp`**

```cpp
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
```

- [ ] **Step 4: Add to `CMakeLists.txt`**

Change:
```cmake
    src/profiles/SystemControl/ScreenControl.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/profiles/SystemControl/ScreenControl.cpp
    src/groups/GroupJson.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors (nothing calls `GroupJson` yet, so this only proves it compiles standalone).

- [ ] **Step 6: Commit**

```bash
git add src/groups/GroupTypes.h src/groups/GroupJson.h src/groups/GroupJson.cpp CMakeLists.txt
git commit -m "Add groups::GroupTypes and GroupJson serialization"
```

---

### Task 3: `groups::GroupStore` (persistence)

**Files:**
- Create: `src/groups/GroupStore.h`
- Create: `src/groups/GroupStore.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/GroupStore.cpp`)

**Interfaces:**
- Consumes: `groups::LaunchGroup` (Task 2), `groups::SerializeGroups`/`DeserializeGroups` (Task 2).
- Produces (used by Task 5 `GroupManager`):
  - `std::vector<groups::LaunchGroup> groups::GroupStore::Load();`
  - `void groups::GroupStore::Save(const std::vector<groups::LaunchGroup>& groups);`

- [ ] **Step 1: Create `src/groups/GroupStore.h`**

```cpp
#pragma once
#include "GroupTypes.h"
#include <vector>

namespace groups {

// Persists launch groups to %LOCALAPPDATA%\MkPCApp\groups.json -- a
// separate file from profiles.json, own schema, no relationship to
// profiles::ProfileStore beyond both living under the same MkPCApp
// folder.
namespace GroupStore {

// Never throws. Returns an empty list on any missing-file/read/parse failure.
std::vector<LaunchGroup> Load();

// Full-file overwrite -- this data changes rarely (group create/edit/
// delete), no need for atomic-write/temp-file complexity. Same idiom as
// profiles::ProfileStore::Save.
void Save(const std::vector<LaunchGroup>& groups);

} // namespace GroupStore

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/GroupStore.cpp`**

```cpp
#include "GroupStore.h"
#include "GroupJson.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace groups {
namespace GroupStore {

namespace {

bool GetLocalAppDataDir(std::wstring& outDir) {
    PWSTR localAppData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
    if (FAILED(hr) || !localAppData) {
        if (localAppData) {
            CoTaskMemFree(localAppData);
        }
        return false;
    }
    outDir = std::wstring(localAppData) + L"\\MkPCApp";
    CoTaskMemFree(localAppData);
    return true;
}

bool EnsureDirectoryExists(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// Empty string means "unavailable" (known-folder lookup failed or the
// directory couldn't be created) -- callers treat that as a no-op.
std::wstring GetGroupsFilePath() {
    std::wstring dir;
    if (!GetLocalAppDataDir(dir)) {
        return L"";
    }
    if (!EnsureDirectoryExists(dir)) {
        return L"";
    }
    return dir + L"\\groups.json";
}

std::string ReadFileContents() {
    std::wstring path = GetGroupsFilePath();
    if (path.empty()) {
        return "";
    }

    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void WriteFileContents(const std::string& contents) {
    std::wstring path = GetGroupsFilePath();
    if (path.empty()) {
        return;
    }

    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }
    file << contents;
}

} // namespace

std::vector<LaunchGroup> Load() {
    std::vector<LaunchGroup> empty;

    std::string contents = ReadFileContents();
    if (contents.empty()) {
        return empty;
    }

    std::vector<LaunchGroup> parsed;
    if (!DeserializeGroups(contents, parsed)) {
        return empty;
    }
    return parsed;
}

void Save(const std::vector<LaunchGroup>& groups) {
    WriteFileContents(SerializeGroups(groups));
}

} // namespace GroupStore
} // namespace groups
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/groups/GroupJson.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/groups/GroupJson.cpp
    src/groups/GroupStore.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/groups/GroupStore.h src/groups/GroupStore.cpp CMakeLists.txt
git commit -m "Add groups::GroupStore persistence"
```

---

### Task 4: Expose `startup::ShortcutStartupControl::ResolveShortcutTarget` publicly

**Files:**
- Modify: `src/startup/ShortcutStartupControl.h`
- Modify: `src/startup/ShortcutStartupControl.cpp`

**Interfaces:**
- Produces (used by Task 9 `GroupEditorDialog`):
  - `std::optional<std::wstring> startup::ShortcutStartupControl::ResolveShortcutTarget(const std::wstring& lnkPath);`

- [ ] **Step 1: Add the declaration to `src/startup/ShortcutStartupControl.h`**

Change:
```cpp
namespace startup::ShortcutStartupControl {

// Both Enumerate* functions resolve each shortcut's target via IShellLink,
// so they must be called with COM already initialized on the calling thread
// (StartupScanner::Scan() owns that pairing for a full rescan).
std::vector<StartupEntry> EnumerateUserStartupFolder();
std::vector<StartupEntry> EnumerateCommonStartupFolder();
```
to:
```cpp
namespace startup::ShortcutStartupControl {

// Resolves a .lnk's target path via IShellLinkW/IPersistFile. Requires COM
// already initialized on the calling thread. Returns nullopt if the
// shortcut can't be loaded/resolved at all (e.g. it points at a non-file
// target like a folder or URL). Exposed publicly (originally private to
// this .cpp) so groups::GroupEditorDialog can resolve a user-picked .lnk
// the same way the Startup-folder scan below does, instead of a second
// copy of the same IShellLinkW dance.
std::optional<std::wstring> ResolveShortcutTarget(const std::wstring& lnkPath);

// Both Enumerate* functions resolve each shortcut's target via IShellLink,
// so they must be called with COM already initialized on the calling thread
// (StartupScanner::Scan() owns that pairing for a full rescan).
std::vector<StartupEntry> EnumerateUserStartupFolder();
std::vector<StartupEntry> EnumerateCommonStartupFolder();
```

Also add `#include <optional>` near the top of the header (alongside the existing `#include "StartupTypes.h"` / `#include <vector>`).

- [ ] **Step 2: Make it public in `src/startup/ShortcutStartupControl.cpp`**

In the anonymous namespace, remove the `std::optional<std::wstring> ResolveShortcutTarget(...)` function (it stays exactly as-is, just moves) and instead define it directly under `namespace ShortcutStartupControl` (outside the anonymous namespace), immediately after the `GetKnownFolder` helper and before `ScanLnkDirectory`. Concretely, change:

```cpp
namespace startup {
namespace ShortcutStartupControl {

namespace {

using platform::WideToUtf8;

std::optional<std::wstring> GetKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &path);
    if (FAILED(hr) || !path) {
        if (path) {
            CoTaskMemFree(path);
        }
        return std::nullopt;
    }
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

// Resolves a .lnk's target path via IShellLinkW/IPersistFile. Requires COM
// to already be initialized on this thread. Returns nullopt if the shortcut
// can't be loaded/resolved at all (e.g. it points at a non-file target like
// a folder or URL) -- the caller still lists the entry, just without a
// resolved exe path.
std::optional<std::wstring> ResolveShortcutTarget(const std::wstring& lnkPath) {
```

to:

```cpp
namespace startup {
namespace ShortcutStartupControl {

namespace {

using platform::WideToUtf8;

std::optional<std::wstring> GetKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &path);
    if (FAILED(hr) || !path) {
        if (path) {
            CoTaskMemFree(path);
        }
        return std::nullopt;
    }
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

} // namespace

std::optional<std::wstring> ResolveShortcutTarget(const std::wstring& lnkPath) {
```

...and after that function's closing `}` (the one that currently returns to the still-open anonymous namespace, right before `void ScanLnkDirectory(...)`), re-open the anonymous namespace for the remaining private helpers:

```cpp
    persistFile->Release();
    shellLink->Release();
    return result;
}

namespace {

void ScanLnkDirectory(const std::wstring& directory, StartupSource source, bool isDisabledSubfolder,
```

Everything else in the file (`ScanLnkDirectory`, `EnumerateFolder`, `EnumerateUserStartupFolder`, `EnumerateCommonStartupFolder`, `SetShortcutEnabled`, `DeleteToRecycleBin`) is unchanged — `ScanLnkDirectory`'s existing call to `ResolveShortcutTarget(fullPath)` still resolves correctly since it's now a sibling function in the same `ShortcutStartupControl` namespace instead of the anonymous one.

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors, and this is a pure reorganization (no behavior change) -- the Startup-folder shortcut scan (`ScanLnkDirectory`) resolves targets exactly as before.

- [ ] **Step 4: Commit**

```bash
git add src/startup/ShortcutStartupControl.h src/startup/ShortcutStartupControl.cpp
git commit -m "Expose ShortcutStartupControl::ResolveShortcutTarget publicly"
```

---

### Task 5: `groups::GroupManager` (CRUD)

**Files:**
- Create: `src/groups/GroupManager.h`
- Create: `src/groups/GroupManager.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/GroupManager.cpp`)

**Interfaces:**
- Consumes: `groups::LaunchGroup`/`LaunchEntry` (Task 2), `groups::GroupStore::Load`/`Save` (Task 3).
- Produces (used by Task 9 `GroupEditorDialog`, Task 10 `ConfirmDeleteGroupDialog`, Task 11 `GroupsTab`):
  - `void groups::GroupManager::Init();`
  - `const std::vector<groups::LaunchGroup>& groups::GroupManager::GetGroups() const;`
  - `std::string groups::GroupManager::CreateGroup(const std::string& name, const std::vector<LaunchEntry>& entries);`
  - `bool groups::GroupManager::UpdateGroup(const std::string& id, const std::string& name, const std::vector<LaunchEntry>& entries);`
  - `bool groups::GroupManager::DeleteGroup(const std::string& id);`

- [ ] **Step 1: Create `src/groups/GroupManager.h`**

```cpp
#pragma once
#include "GroupTypes.h"
#include <string>
#include <vector>

namespace groups {

// Owns the full list of user-defined launch groups, loaded once from
// GroupStore and kept in memory afterward -- every CRUD method persists
// immediately. Unlike profiles::ProfileManager, this needs no mutex:
// nothing on the background data-tick thread reads or writes group
// definitions (GroupProcessTracker::Tick only touches runtime process
// state, never this class) -- only ever touched from the render thread,
// via GroupsTab's button/dialog handlers.
class GroupManager {
public:
    void Init(); // loads groups_ from GroupStore::Load()

    const std::vector<LaunchGroup>& GetGroups() const { return groups_; }

    // Generates a fresh id ("group.<guid>"), appends, persists, returns
    // the new id.
    std::string CreateGroup(const std::string& name, const std::vector<LaunchEntry>& entries);

    // Updates an existing group in place and persists. Returns false
    // (no-op) if id isn't found.
    bool UpdateGroup(const std::string& id, const std::string& name, const std::vector<LaunchEntry>& entries);

    // Removes a group and persists. Returns false (no-op) if id isn't found.
    bool DeleteGroup(const std::string& id);

private:
    static std::string GenerateGroupId();
    std::vector<LaunchGroup> groups_;
};

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/GroupManager.cpp`**

```cpp
#include "GroupManager.h"
#include "GroupStore.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>

namespace groups {

void GroupManager::Init() {
    groups_ = GroupStore::Load();
}

std::string GroupManager::GenerateGroupId() {
    GUID guid;
    // Falls back to a fixed placeholder in the (essentially impossible)
    // event CoCreateGuid fails, rather than leaving groups_ with a
    // duplicate/empty id -- same idiom as
    // profiles::ProfileManager::GenerateCustomProfileId.
    if (FAILED(CoCreateGuid(&guid))) {
        return "group.fallback";
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "group.%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
                  guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return std::string(buffer);
}

std::string GroupManager::CreateGroup(const std::string& name, const std::vector<LaunchEntry>& entries) {
    LaunchGroup group;
    group.id = GenerateGroupId();
    group.name = name;
    group.entries = entries;

    groups_.push_back(group);
    GroupStore::Save(groups_);
    return group.id;
}

bool GroupManager::UpdateGroup(const std::string& id, const std::string& name,
                                const std::vector<LaunchEntry>& entries) {
    for (LaunchGroup& group : groups_) {
        if (group.id == id) {
            group.name = name;
            group.entries = entries;
            GroupStore::Save(groups_);
            return true;
        }
    }
    return false;
}

bool GroupManager::DeleteGroup(const std::string& id) {
    auto it = std::find_if(groups_.begin(), groups_.end(),
                            [&id](const LaunchGroup& g) { return g.id == id; });
    if (it == groups_.end()) {
        return false;
    }
    groups_.erase(it);
    GroupStore::Save(groups_);
    return true;
}

} // namespace groups
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/groups/GroupStore.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/groups/GroupStore.cpp
    src/groups/GroupManager.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/groups/GroupManager.h src/groups/GroupManager.cpp CMakeLists.txt
git commit -m "Add groups::GroupManager CRUD"
```

---

### Task 6: `groups::ProcessLifecycle` (Win32 process primitives)

**Files:**
- Create: `src/groups/ProcessLifecycle.h`
- Create: `src/groups/ProcessLifecycle.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/ProcessLifecycle.cpp`)

**Interfaces:**
- Produces (used by Task 7 `GroupProcessTracker` and Task 8 `GroupLauncher`):
  - `bool groups::IsProcessRunning(const std::wstring& resolvedExePath);`
  - `struct groups::LaunchResult { bool ok; DWORD pid; HANDLE processHandle; };`
  - `groups::LaunchResult groups::LaunchProcess(const std::wstring& exePath, const std::wstring& args);`
  - `void groups::RequestGracefulClose(DWORD pid);`
  - `bool groups::IsProcessAlive(HANDLE processHandle);`
  - `bool groups::ForceTerminate(HANDLE processHandle);`

- [ ] **Step 1: Create `src/groups/ProcessLifecycle.h`**

```cpp
#pragma once
#include <windows.h>
#include <string>

namespace groups {

// True if any running process' full image path matches resolvedExePath
// case-insensitively -- takes a fresh CreateToolhelp32Snapshot each call
// (TH32CS_SNAPPROCESS) plus one QueryFullProcessImageNameW per process,
// so only meant for a low-frequency, user-triggered check (the "Abrir
// grupo" click), never per-frame.
bool IsProcessRunning(const std::wstring& resolvedExePath);

struct LaunchResult {
    bool ok = false;
    DWORD pid = 0;
    // Valid only if ok is true. Caller (GroupProcessTracker) takes
    // ownership -- must CloseHandle once it's done tracking this process.
    HANDLE processHandle = nullptr;
};

// CreateProcess wrapper. Working directory is set to exePath's own folder
// (many GUI apps assume their own directory as CWD to find adjacent
// resources) -- unlike BridgeProcess's hidden, IDLE_PRIORITY sensor
// bridge, this must show its own window normally (it's launching
// user-facing apps like a game or Discord), so no CREATE_NO_WINDOW /
// priority-class flags.
LaunchResult LaunchProcess(const std::wstring& exePath, const std::wstring& args);

// Sends WM_CLOSE to every visible top-level window owned by `pid` -- does
// not wait for the process to actually exit. Never blocks (called from
// GroupProcessTracker::ReleaseOwnership on the render thread) -- the
// caller polls IsProcessAlive on a later tick and force-closes after a
// timeout instead.
void RequestGracefulClose(DWORD pid);

// Non-blocking liveness check (WaitForSingleObject with a zero timeout).
bool IsProcessAlive(HANDLE processHandle);

// Returns false only if TerminateProcess itself reports failure (e.g. no
// permission) -- a process that had already exited on its own still
// counts as success, nothing left to terminate.
bool ForceTerminate(HANDLE processHandle);

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/ProcessLifecycle.cpp`**

```cpp
#include "ProcessLifecycle.h"
#include <tlhelp32.h>
#include <vector>

namespace groups {

namespace {

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

struct EnumWindowsContext {
    DWORD targetPid;
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectTopLevelWindowsForPid(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumWindowsContext*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == ctx->targetPid && IsWindowVisible(hwnd)) {
        ctx->windows.push_back(hwnd);
    }
    return TRUE;
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t lastSlash = path.find_last_of(L"\\/");
    return (lastSlash == std::wstring::npos) ? L"" : path.substr(0, lastSlash);
}

} // namespace

bool IsProcessRunning(const std::wstring& resolvedExePath) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool found = false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) {
                continue;
            }
            wchar_t imagePath[MAX_PATH];
            DWORD imagePathLen = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, imagePath, &imagePathLen) &&
                PathsEqual(std::wstring(imagePath, imagePathLen), resolvedExePath)) {
                found = true;
            }
            CloseHandle(process);
        } while (!found && Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

LaunchResult LaunchProcess(const std::wstring& exePath, const std::wstring& args) {
    LaunchResult result;

    std::wstring commandLine = L"\"" + exePath + L"\"";
    if (!args.empty()) {
        commandLine += L" " + args;
    }
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    std::wstring workingDir = DirectoryOf(exePath);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    BOOL ok = CreateProcessW(exePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                              workingDir.empty() ? nullptr : workingDir.c_str(), &startupInfo, &processInfo);
    if (!ok) {
        return result;
    }

    CloseHandle(processInfo.hThread);
    result.ok = true;
    result.pid = processInfo.dwProcessId;
    result.processHandle = processInfo.hProcess;
    return result;
}

void RequestGracefulClose(DWORD pid) {
    EnumWindowsContext ctx{pid, {}};
    EnumWindows(CollectTopLevelWindowsForPid, reinterpret_cast<LPARAM>(&ctx));
    for (HWND hwnd : ctx.windows) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

bool IsProcessAlive(HANDLE processHandle) {
    if (!processHandle) {
        return false;
    }
    return WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
}

bool ForceTerminate(HANDLE processHandle) {
    if (!processHandle) {
        return true;
    }
    if (!IsProcessAlive(processHandle)) {
        return true;
    }
    return TerminateProcess(processHandle, 1) != 0;
}

} // namespace groups
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/groups/GroupManager.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/groups/GroupManager.cpp
    src/groups/ProcessLifecycle.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors. `user32.lib`/`kernel32.lib` (needed for `EnumWindows`/`CreateToolhelp32Snapshot`/`CreateProcessW`) are already part of MSVC's default link libraries -- `src/main.cpp` already calls `RegisterClassExW`/`CreateWindowExW` without `user32` being explicitly listed in `target_link_libraries`, confirming this.

- [ ] **Step 5: Commit**

```bash
git add src/groups/ProcessLifecycle.h src/groups/ProcessLifecycle.cpp CMakeLists.txt
git commit -m "Add groups::ProcessLifecycle Win32 process primitives"
```

---

### Task 7: `groups::GroupProcessTracker` (cross-group reference counting)

**Files:**
- Create: `src/groups/GroupProcessTracker.h`
- Create: `src/groups/GroupProcessTracker.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/GroupProcessTracker.cpp`)

**Interfaces:**
- Consumes: `groups::RequestGracefulClose`, `groups::IsProcessAlive`, `groups::ForceTerminate` (Task 6).
- Produces (used by Task 8 `GroupLauncher` and Task 11 `GroupsTab`):
  - `bool groups::GroupProcessTracker::IsPathCurrentlyOwned(const std::wstring& resolvedExePath) const;`
  - `void groups::GroupProcessTracker::JoinExistingOwnership(const std::string& groupId, const std::wstring& resolvedExePath);`
  - `void groups::GroupProcessTracker::RegisterOwnedLaunch(const std::string& groupId, const std::wstring& resolvedExePath, DWORD pid, HANDLE processHandle);`
  - `void groups::GroupProcessTracker::ReleaseOwnership(const std::string& groupId, const std::wstring& resolvedExePath, uint64_t nowTickCount);`
  - `void groups::GroupProcessTracker::Tick(uint64_t tickCount);`
  - `std::vector<std::wstring> groups::GroupProcessTracker::ConsumeCloseFailures();`

- [ ] **Step 1: Create `src/groups/GroupProcessTracker.h`**

```cpp
#pragma once
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace groups {

// Cross-group reference counting for processes this app itself launched
// via GroupLauncher, keyed by resolved executable path (not PID, since
// relaunching the same app produces a different PID). Two groups that
// both list the same app (e.g. Discord in both "League of Legends" and
// "Minecraft") share one entry here -- the process is only actually
// closed once no currently-open group still owns it. A process the user
// already had running before any group's "Abrir grupo" was clicked is
// never registered here at all, so ReleaseOwnership never touches it --
// see the design spec's corrected "Flujo Abrir grupo".
//
// Also owns the "graceful close, then force after a timeout" bookkeeping:
// ReleaseOwnership requests a graceful WM_CLOSE immediately (via
// groups::RequestGracefulClose) and schedules a force-terminate check a
// few ticks later; Tick() (called from GroupsTab::OnTick, the background
// 1 Hz thread) carries out that force-terminate and records any
// failures, drained via ConsumeCloseFailures(). Every field here is
// in-memory only -- see the design spec's "estado abierto/cerrado...
// no persiste" limitation: a MkPCApp.exe restart loses this bookkeeping
// (the launched apps themselves keep running -- see the destructor).
//
// Every public method locks mutex_ -- safe to call from both the render
// thread (RegisterOwnedLaunch/JoinExistingOwnership/ReleaseOwnership/
// IsPathCurrentlyOwned, from GroupsTab's button handlers) and the
// background data-tick thread (Tick/ConsumeCloseFailures).
class GroupProcessTracker {
public:
    // Releases (but does not terminate) every process handle still
    // tracked at shutdown -- MkPCApp.exe exiting must never kill apps a
    // group launched (a game, Discord, ...); only closing the group
    // explicitly does that.
    ~GroupProcessTracker();

    bool IsPathCurrentlyOwned(const std::wstring& resolvedExePath) const;

    // Joins groupId onto the existing owner set for resolvedExePath
    // without launching anything -- used when OpenGroup finds the path
    // already tracked (owned by another currently-open group). No-op if
    // resolvedExePath isn't tracked at all (caller bug -- must check
    // IsPathCurrentlyOwned first).
    void JoinExistingOwnership(const std::string& groupId, const std::wstring& resolvedExePath);

    // Registers groupId as the (first) owner of a process this app just
    // launched. Call only right after LaunchProcess succeeded for a path
    // that wasn't already tracked.
    void RegisterOwnedLaunch(const std::string& groupId, const std::wstring& resolvedExePath, DWORD pid,
                              HANDLE processHandle);

    // Removes groupId from the owners of resolvedExePath. No-op if
    // groupId never owned it (e.g. it was external, or the launch had
    // failed). If this was the last owner, requests a graceful close now
    // and schedules a force-terminate check for
    // `nowTickCount + kForceCloseDelayTicks`.
    void ReleaseOwnership(const std::string& groupId, const std::wstring& resolvedExePath, uint64_t nowTickCount);

    // Called once per tick (~1 Hz) from the background data-tick thread.
    // Force-terminates any pending close whose deadline has passed and
    // the process is still alive.
    void Tick(uint64_t tickCount);

    // Drains the list of paths that failed to force-close since the last
    // call -- GroupsTab surfaces these as a transient error message.
    std::vector<std::wstring> ConsumeCloseFailures();

private:
    struct OwnedProcess {
        DWORD pid = 0;
        HANDLE processHandle = nullptr;
        std::set<std::string> ownerGroupIds;
    };
    struct PendingForceClose {
        std::wstring resolvedExePath;
        HANDLE processHandle = nullptr;
        uint64_t deadlineTick = 0;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::wstring, OwnedProcess> ownedByPath_;
    std::vector<PendingForceClose> pendingForceClose_;
    std::vector<std::wstring> closeFailures_;
};

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/GroupProcessTracker.cpp`**

```cpp
#include "GroupProcessTracker.h"
#include "ProcessLifecycle.h"

namespace groups {

namespace {
// ~5 ticks at the app's 1 Hz data-tick rate -- "esperar unos segundos"
// per the design spec's "Flujo Cerrar grupo".
constexpr uint64_t kForceCloseDelayTicks = 5;
} // namespace

GroupProcessTracker::~GroupProcessTracker() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [path, owned] : ownedByPath_) {
        if (owned.processHandle) {
            CloseHandle(owned.processHandle);
        }
    }
    for (auto& pending : pendingForceClose_) {
        if (pending.processHandle) {
            CloseHandle(pending.processHandle);
        }
    }
}

bool GroupProcessTracker::IsPathCurrentlyOwned(const std::wstring& resolvedExePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ownedByPath_.find(resolvedExePath) != ownedByPath_.end();
}

void GroupProcessTracker::JoinExistingOwnership(const std::string& groupId,
                                                 const std::wstring& resolvedExePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownedByPath_.find(resolvedExePath);
    if (it == ownedByPath_.end()) {
        return; // caller bug -- should have checked IsPathCurrentlyOwned first
    }
    it->second.ownerGroupIds.insert(groupId);
}

void GroupProcessTracker::RegisterOwnedLaunch(const std::string& groupId, const std::wstring& resolvedExePath,
                                               DWORD pid, HANDLE processHandle) {
    std::lock_guard<std::mutex> lock(mutex_);
    OwnedProcess& owned = ownedByPath_[resolvedExePath];
    owned.pid = pid;
    owned.processHandle = processHandle;
    owned.ownerGroupIds.insert(groupId);
}

void GroupProcessTracker::ReleaseOwnership(const std::string& groupId, const std::wstring& resolvedExePath,
                                            uint64_t nowTickCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownedByPath_.find(resolvedExePath);
    if (it == ownedByPath_.end()) {
        return; // never tracked by anyone (external, or the launch had failed)
    }

    it->second.ownerGroupIds.erase(groupId);
    if (!it->second.ownerGroupIds.empty()) {
        return; // another open group still owns it -- leave it running
    }

    DWORD pid = it->second.pid;
    HANDLE handle = it->second.processHandle;
    ownedByPath_.erase(it);

    RequestGracefulClose(pid); // non-blocking, sends WM_CLOSE now

    PendingForceClose pending;
    pending.resolvedExePath = resolvedExePath;
    pending.processHandle = handle;
    pending.deadlineTick = nowTickCount + kForceCloseDelayTicks;
    pendingForceClose_.push_back(std::move(pending));
}

void GroupProcessTracker::Tick(uint64_t tickCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pendingForceClose_.begin(); it != pendingForceClose_.end();) {
        if (!IsProcessAlive(it->processHandle)) {
            CloseHandle(it->processHandle);
            it = pendingForceClose_.erase(it);
            continue;
        }
        if (tickCount >= it->deadlineTick) {
            if (!ForceTerminate(it->processHandle)) {
                closeFailures_.push_back(it->resolvedExePath);
            }
            CloseHandle(it->processHandle);
            it = pendingForceClose_.erase(it);
            continue;
        }
        ++it;
    }
}

std::vector<std::wstring> GroupProcessTracker::ConsumeCloseFailures() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> result = std::move(closeFailures_);
    closeFailures_.clear();
    return result;
}

} // namespace groups
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/groups/ProcessLifecycle.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/groups/ProcessLifecycle.cpp
    src/groups/GroupProcessTracker.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/groups/GroupProcessTracker.h src/groups/GroupProcessTracker.cpp CMakeLists.txt
git commit -m "Add groups::GroupProcessTracker reference counting"
```

---

### Task 8: `groups::GroupLauncher` (open/close orchestration)

**Files:**
- Create: `src/groups/GroupLauncher.h`
- Create: `src/groups/GroupLauncher.cpp`
- Modify: `CMakeLists.txt` (add `src/groups/GroupLauncher.cpp`)

**Interfaces:**
- Consumes: `groups::LaunchEntry` (Task 2); `groups::GroupProcessTracker` (Task 7); `groups::IsProcessRunning`/`LaunchProcess` (Task 6); `platform::Utf8ToWide` (existing, `src/platform/StringConvert.h`).
- Produces (used by Task 11 `GroupsTab`):
  - `enum class groups::EntryOpenStatus { Launched, JoinedExisting, ExternallyOwned, LaunchFailed };`
  - `struct groups::EntryOpenOutcome { EntryOpenStatus status; };`
  - `std::vector<groups::EntryOpenOutcome> groups::GroupLauncher::OpenGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries);`
  - `void groups::GroupLauncher::CloseGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries, uint64_t nowTickCount);`

- [ ] **Step 1: Create `src/groups/GroupLauncher.h`**

```cpp
#pragma once
#include "GroupProcessTracker.h"
#include "GroupTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace groups {

enum class EntryOpenStatus {
    Launched,          // this app started it, now sole owner
    JoinedExisting,     // already running, owned by another currently-open group -- joined as co-owner
    ExternallyOwned,    // already running for a reason the tracker doesn't know (e.g. user opened it) -- untouched
    LaunchFailed,       // CreateProcess failed, or the entry had no resolved exe path
};

struct EntryOpenOutcome {
    EntryOpenStatus status = EntryOpenStatus::LaunchFailed;
};

// Orchestrates opening/closing one LaunchGroup's entries, delegating
// cross-group reference counting to a GroupProcessTracker shared by
// every group in the app (owned by GroupsTab, constructed once, passed
// here by reference). Every method here is only ever called from the
// render thread (GroupsTab's button handlers) -- see
// GroupProcessTracker's own threading note for why Tick() is the one
// method called from the background thread instead.
class GroupLauncher {
public:
    explicit GroupLauncher(GroupProcessTracker& tracker) : tracker_(tracker) {}

    // One outcome per entry, same order as `entries`, so the caller can
    // show a per-entry error/note without aborting the rest of the group.
    std::vector<EntryOpenOutcome> OpenGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries);

    // Releases this group's ownership of every entry (a no-op per entry
    // if this group never owned it -- see GroupProcessTracker::
    // ReleaseOwnership). Actual process closing/force-close bookkeeping
    // happens inside tracker_; see GroupProcessTracker::Tick.
    void CloseGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries, uint64_t nowTickCount);

private:
    GroupProcessTracker& tracker_;
};

} // namespace groups
```

- [ ] **Step 2: Create `src/groups/GroupLauncher.cpp`**

```cpp
#include "GroupLauncher.h"
#include "ProcessLifecycle.h"
#include "../platform/StringConvert.h"

namespace groups {

std::vector<EntryOpenOutcome> GroupLauncher::OpenGroup(const std::string& groupId,
                                                        const std::vector<LaunchEntry>& entries) {
    std::vector<EntryOpenOutcome> outcomes;
    outcomes.reserve(entries.size());

    for (const LaunchEntry& entry : entries) {
        EntryOpenOutcome outcome;

        if (entry.resolvedExePath.empty()) {
            outcome.status = EntryOpenStatus::LaunchFailed;
            outcomes.push_back(outcome);
            continue;
        }

        std::wstring widePath = platform::Utf8ToWide(entry.resolvedExePath);

        // Already claimed by another currently-open group -- join as
        // co-owner, don't relaunch. See the design spec's corrected
        // "Flujo Abrir grupo" for why this check comes before the
        // system-wide IsProcessRunning check below.
        if (tracker_.IsPathCurrentlyOwned(widePath)) {
            tracker_.JoinExistingOwnership(groupId, widePath);
            outcome.status = EntryOpenStatus::JoinedExisting;
            outcomes.push_back(outcome);
            continue;
        }

        // Running for a reason the tracker doesn't know about (normally:
        // the user already had it open) -- leave it alone entirely.
        if (IsProcessRunning(widePath)) {
            outcome.status = EntryOpenStatus::ExternallyOwned;
            outcomes.push_back(outcome);
            continue;
        }

        LaunchResult launch = LaunchProcess(widePath, platform::Utf8ToWide(entry.args));
        if (!launch.ok) {
            outcome.status = EntryOpenStatus::LaunchFailed;
            outcomes.push_back(outcome);
            continue;
        }

        tracker_.RegisterOwnedLaunch(groupId, widePath, launch.pid, launch.processHandle);
        outcome.status = EntryOpenStatus::Launched;
        outcomes.push_back(outcome);
    }

    return outcomes;
}

void GroupLauncher::CloseGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries,
                                uint64_t nowTickCount) {
    for (const LaunchEntry& entry : entries) {
        if (entry.resolvedExePath.empty()) {
            continue;
        }
        tracker_.ReleaseOwnership(groupId, platform::Utf8ToWide(entry.resolvedExePath), nowTickCount);
    }
}

} // namespace groups
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/groups/GroupProcessTracker.cpp
    src/startup/RegistryStartupControl.cpp
```
to:
```cmake
    src/groups/GroupProcessTracker.cpp
    src/groups/GroupLauncher.cpp
    src/startup/RegistryStartupControl.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/groups/GroupLauncher.h src/groups/GroupLauncher.cpp CMakeLists.txt
git commit -m "Add groups::GroupLauncher open/close orchestration"
```

---

### Task 9: `ui::GroupEditorDialog` (create/edit a group)

**Files:**
- Create: `src/ui/GroupEditorDialog.h`
- Create: `src/ui/GroupEditorDialog.cpp`
- Modify: `CMakeLists.txt` (add `src/ui/GroupEditorDialog.cpp`)

**Interfaces:**
- Consumes: `groups::GroupManager` (Task 5), `groups::LaunchGroup`/`LaunchEntry` (Task 2), `startup::ShortcutStartupControl::ResolveShortcutTarget` (Task 4), `platform::Utf8ToWide`/`WideToUtf8`, `platform::ComScope`.
- Produces (used by Task 11 `GroupsTab`):
  - `void ui::GroupEditorDialog::OpenForCreate();`
  - `void ui::GroupEditorDialog::OpenForEdit(const groups::LaunchGroup& group);`
  - `void ui::GroupEditorDialog::Render(groups::GroupManager& manager);`
  - `bool ui::GroupEditorDialog::ConsumeJustSaved();`

- [ ] **Step 1: Create `src/ui/GroupEditorDialog.h`**

```cpp
#pragma once
#include "../groups/GroupManager.h"
#include "../groups/GroupTypes.h"
#include <optional>
#include <string>
#include <vector>

namespace ui {

// Modal create/edit dialog for launch groups, driven by GroupsTab. Same
// idiom as ProfileEditorDialog/AddStartupEntryDialog: OpenForCreate/
// OpenForEdit call ImGui::OpenPopup, Render() must run every frame
// regardless of whether the dialog is open. Unlike AddStartupEntryDialog's
// file picker (filtered to *.exe only), this one accepts any file --
// launch groups cover cases like Minecraft that run through a Java
// launcher, not a bare .exe.
class GroupEditorDialog {
public:
    void OpenForCreate();
    void OpenForEdit(const groups::LaunchGroup& group);

    void Render(groups::GroupManager& manager);

    // Returns true exactly once, right after a successful save, then
    // resets -- lets GroupsTab refresh its view immediately.
    bool ConsumeJustSaved();

private:
    // Returns true if this row's "Quitar" was clicked -- caller removes
    // it after the loop finishes rather than erasing entries_ mid-iteration.
    bool RenderEntryRow(size_t index);

    bool isOpen_ = false;
    bool openRequested_ = false;
    bool isEditMode_ = false;
    bool justSaved_ = false;
    std::string editingGroupId_;
    char nameBuffer_[128] = "";
    std::vector<groups::LaunchEntry> entries_;
};

} // namespace ui
```

- [ ] **Step 2: Create `src/ui/GroupEditorDialog.cpp`**

```cpp
#include "GroupEditorDialog.h"
#include "../platform/ComScope.h"
#include "../platform/StringConvert.h"
#include "../startup/ShortcutStartupControl.h"
#include <imgui.h>
#include <shobjidl.h>
#include <cstring>
#include <optional>

namespace ui {

namespace {

using platform::Utf8ToWide;
using platform::WideToUtf8;

constexpr const char* kPopupId = "Grupo de lanzamiento###GroupEditorDialog";

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value) {
    std::strncpy(buffer, value.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

// Same rare, user-triggered COM usage as AddStartupEntryDialog's file
// picker -- bracketed locally, not part of any per-cycle ComScope
// pairing. Unlike that one, accepts any file (no SetFileTypes() filter):
// a launch-group entry can be a .exe, a .lnk, or anything else the OS
// can execute (e.g. Minecraft launches through Java, not a bare .exe).
std::optional<std::wstring> PickAnyFile(HWND owner) {
    platform::ComScope comScope;

    std::optional<std::wstring> result;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                   reinterpret_cast<void**>(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    result = std::wstring(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }

    return result;
}

bool HasExtension(const std::wstring& path, const wchar_t* ext) {
    size_t extLen = wcslen(ext);
    if (path.size() < extLen) {
        return false;
    }
    return _wcsnicmp(path.c_str() + (path.size() - extLen), ext, extLen) == 0;
}

// Resolves a picked path to the exe that will actually run: a .lnk is
// resolved to its target via the same IShellLinkW logic StartupTab's
// Startup-folder scan uses; anything else (a .exe, or any other
// executable format the user picked) is used as-is.
std::wstring ResolveEntryExePath(const std::wstring& pickedPath) {
    if (!HasExtension(pickedPath, L".lnk")) {
        return pickedPath;
    }
    platform::ComScope comScope;
    std::optional<std::wstring> target = startup::ShortcutStartupControl::ResolveShortcutTarget(pickedPath);
    return target.value_or(L"");
}

} // namespace

void GroupEditorDialog::OpenForCreate() {
    isOpen_ = true;
    isEditMode_ = false;
    editingGroupId_.clear();
    entries_.clear();
    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), "");
    openRequested_ = true;
}

void GroupEditorDialog::OpenForEdit(const groups::LaunchGroup& group) {
    isOpen_ = true;
    isEditMode_ = true;
    editingGroupId_ = group.id;
    entries_ = group.entries;
    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), group.name);
    openRequested_ = true;
}

void GroupEditorDialog::Render(groups::GroupManager& manager) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::InputText("Nombre", nameBuffer_, sizeof(nameBuffer_));

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextUnformatted("Apps del grupo:");

    std::optional<size_t> pendingRemoveIndex;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (RenderEntryRow(i)) {
            pendingRemoveIndex = i;
        }
    }
    if (pendingRemoveIndex.has_value()) {
        entries_.erase(entries_.begin() + static_cast<long>(*pendingRemoveIndex));
    }

    if (ImGui::SmallButton("+ Anadir app")) {
        std::optional<std::wstring> picked = PickAnyFile(nullptr);
        if (picked.has_value()) {
            groups::LaunchEntry entry;
            entry.path = WideToUtf8(*picked);
            entry.resolvedExePath = WideToUtf8(ResolveEntryExePath(*picked));
            entries_.push_back(std::move(entry));
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    bool canSave = nameBuffer_[0] != '\0' && !entries_.empty();
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Guardar")) {
        if (isEditMode_) {
            manager.UpdateGroup(editingGroupId_, nameBuffer_, entries_);
        } else {
            manager.CreateGroup(nameBuffer_, entries_);
        }
        isOpen_ = false;
        justSaved_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool GroupEditorDialog::RenderEntryRow(size_t index) {
    ImGui::PushID(static_cast<int>(index));
    groups::LaunchEntry& entry = entries_[index];

    bool resolved = !entry.resolvedExePath.empty();
    ImGui::TextWrapped("%s", resolved ? entry.resolvedExePath.c_str() : "(no se pudo resolver el destino)");

    char argsBuffer[256];
    CopyToBuffer(argsBuffer, sizeof(argsBuffer), entry.args);
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::InputText("Argumentos", argsBuffer, sizeof(argsBuffer))) {
        entry.args = argsBuffer;
    }

    ImGui::SameLine();
    bool removeClicked = ImGui::SmallButton("Quitar");

    ImGui::Separator();
    ImGui::PopID();
    return removeClicked;
}

bool GroupEditorDialog::ConsumeJustSaved() {
    bool result = justSaved_;
    justSaved_ = false;
    return result;
}

} // namespace ui
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/ui/StartupTab.cpp
    src/ui/AddStartupEntryDialog.cpp
```
to:
```cmake
    src/ui/StartupTab.cpp
    src/ui/GroupEditorDialog.cpp
    src/ui/AddStartupEntryDialog.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/GroupEditorDialog.h src/ui/GroupEditorDialog.cpp CMakeLists.txt
git commit -m "Add ui::GroupEditorDialog"
```

---

### Task 10: `ui::ConfirmDeleteGroupDialog`

**Files:**
- Create: `src/ui/ConfirmDeleteGroupDialog.h`
- Create: `src/ui/ConfirmDeleteGroupDialog.cpp`
- Modify: `CMakeLists.txt` (add `src/ui/ConfirmDeleteGroupDialog.cpp`)

**Interfaces:**
- Consumes: `groups::GroupManager::DeleteGroup` (Task 5), `groups::GroupLauncher::CloseGroup` (Task 8), `groups::LaunchGroup` (Task 2).
- Produces (used by Task 11 `GroupsTab`):
  - `void ui::ConfirmDeleteGroupDialog::OpenForGroup(const groups::LaunchGroup& group);`
  - `void ui::ConfirmDeleteGroupDialog::Render(groups::GroupManager& manager, groups::GroupLauncher& launcher, const std::unordered_set<std::string>& openGroupIds, uint64_t tickCount);`
  - `std::optional<std::string> ui::ConfirmDeleteGroupDialog::ConsumeJustDeleted();`

- [ ] **Step 1: Create `src/ui/ConfirmDeleteGroupDialog.h`**

```cpp
#pragma once
#include "../groups/GroupLauncher.h"
#include "../groups/GroupManager.h"
#include "../groups/GroupTypes.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

namespace ui {

// Confirmation modal shown before deleting a launch group. Same idiom as
// startup::ConfirmDeleteDialog (kept as a separate small class rather
// than shared/templated, since that one is coupled to
// startup::StartupEntry/StartupScanner): OpenForGroup() captures its own
// copy of the group (not a live reference -- the underlying list can
// mutate between opening this dialog and the user confirming), Render()
// is called unconditionally every frame.
//
// If the group being deleted is currently open (per openGroupIds,
// GroupsTab's own tracking), confirming the delete closes it first (same
// as pressing "Cerrar grupo") so no process is left orphaned in
// GroupProcessTracker -- see the design spec's UI section.
class ConfirmDeleteGroupDialog {
public:
    void OpenForGroup(const groups::LaunchGroup& group);
    void Render(groups::GroupManager& manager, groups::GroupLauncher& launcher,
                const std::unordered_set<std::string>& openGroupIds, uint64_t tickCount);

    // Returns the id of the group just deleted, exactly once, then
    // resets to nullopt -- lets GroupsTab drop it from its own "which
    // groups are open" tracking and refresh its view immediately.
    std::optional<std::string> ConsumeJustDeleted();

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    std::optional<std::string> justDeletedId_;
    groups::LaunchGroup pendingGroup_;
};

} // namespace ui
```

- [ ] **Step 2: Create `src/ui/ConfirmDeleteGroupDialog.cpp`**

```cpp
#include "ConfirmDeleteGroupDialog.h"
#include <imgui.h>

namespace ui {

namespace {
constexpr const char* kPopupId = "Eliminar grupo###ConfirmDeleteGroupDialog";
} // namespace

void ConfirmDeleteGroupDialog::OpenForGroup(const groups::LaunchGroup& group) {
    pendingGroup_ = group;
    isOpen_ = true;
    openRequested_ = true;
}

void ConfirmDeleteGroupDialog::Render(groups::GroupManager& manager, groups::GroupLauncher& launcher,
                                       const std::unordered_set<std::string>& openGroupIds, uint64_t tickCount) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Seguro que quieres eliminar el grupo \"%s\"?", pendingGroup_.name.c_str());
    ImGui::TextWrapped("Esto no cierra ninguna app que otro grupo siga usando -- solo borra la definicion del grupo.");

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ImGui::Button("Eliminar")) {
        if (openGroupIds.count(pendingGroup_.id) > 0) {
            launcher.CloseGroup(pendingGroup_.id, pendingGroup_.entries, tickCount);
        }
        manager.DeleteGroup(pendingGroup_.id);
        justDeletedId_ = pendingGroup_.id;
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

std::optional<std::string> ConfirmDeleteGroupDialog::ConsumeJustDeleted() {
    std::optional<std::string> result = justDeletedId_;
    justDeletedId_.reset();
    return result;
}

} // namespace ui
```

- [ ] **Step 3: Add to `CMakeLists.txt`**

Change:
```cmake
    src/ui/GroupEditorDialog.cpp
    src/ui/AddStartupEntryDialog.cpp
```
to:
```cmake
    src/ui/GroupEditorDialog.cpp
    src/ui/ConfirmDeleteGroupDialog.cpp
    src/ui/AddStartupEntryDialog.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ConfirmDeleteGroupDialog.h src/ui/ConfirmDeleteGroupDialog.cpp CMakeLists.txt
git commit -m "Add ui::ConfirmDeleteGroupDialog"
```

---

### Task 11: `ui::GroupsTab` (the tab itself) + shared icon placeholder + app registration

**Files:**
- Modify: `src/ui/CardWidgets.h` (add a shared `RenderIconPlaceholder`)
- Modify: `src/ui/CardWidgets.cpp`
- Modify: `src/ui/StartupTab.cpp` (use the now-shared helper instead of its own private copy)
- Create: `src/ui/GroupsTab.h`
- Create: `src/ui/GroupsTab.cpp`
- Modify: `src/app/Application.cpp` (register the new tab)
- Modify: `CMakeLists.txt` (add `src/ui/GroupsTab.cpp`)

**Interfaces:**
- Consumes: everything from Tasks 2, 5, 6, 7, 8, 9, 10; `platform::IconTextureCache`/`platform::DX11Renderer` (existing); `ui::ITab` (existing).
- Produces: `ui::GroupsTab` registered as a new sidebar tab. Nothing else consumes this module.

- [ ] **Step 1: Move `RenderIconPlaceholder` into `src/ui/CardWidgets.h`/`.cpp`**

In `src/ui/CardWidgets.h`, change:
```cpp
constexpr float kCardWidth = 160.0f;

// Centers a single line of text horizontally within the current content
```
to:
```cpp
constexpr float kCardWidth = 160.0f;

// Drawn instead of ImGui::Image() when icon extraction failed or an
// entry's target is missing -- degrade visibly rather than showing
// nothing or a broken image. Shared between StartupTab and GroupsTab,
// both of which render a resolved-exe-path icon per row/card.
void RenderIconPlaceholder(float size);

// Centers a single line of text horizontally within the current content
```

In `src/ui/CardWidgets.cpp`, add (after the includes, before `CenteredText`):
```cpp
void RenderIconPlaceholder(float size) {
    ImGui::Dummy(ImVec2(size, size));
    ImVec2 boxMin = ImGui::GetItemRectMin();
    ImVec2 boxMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 textSize = ImGui::CalcTextSize("?");
    ImVec2 textPos(boxMin.x + (size - textSize.x) * 0.5f, boxMin.y + (size - textSize.y) * 0.5f);
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
}
```

In `src/ui/StartupTab.cpp`, remove the now-duplicate local function and update its one call site. Change:
```cpp
const char* SourceBadgeText(startup::StartupSource source) {
    switch (source) {
        case startup::StartupSource::RegistryHkcuRun:
            return "Registro (tu usuario)";
        case startup::StartupSource::RegistryHklmRun:
            return "Registro (todos los usuarios)";
        case startup::StartupSource::StartupFolderUser:
            return "Carpeta Inicio";
        case startup::StartupSource::StartupFolderCommon:
            return "Carpeta Inicio (todos)";
    }
    return "";
}

// Drawn instead of ImGui::Image() when icon extraction failed or the
// entry's target is missing -- degrade visibly rather than showing nothing
// or a broken image, same principle the hardware monitor uses for missing
// sensors.
void RenderIconPlaceholder() {
    ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    ImVec2 boxMin = ImGui::GetItemRectMin();
    ImVec2 boxMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border));
    ImVec2 textSize = ImGui::CalcTextSize("?");
    ImVec2 textPos(boxMin.x + (kIconSize - textSize.x) * 0.5f, boxMin.y + (kIconSize - textSize.y) * 0.5f);
    drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "?");
}

} // namespace
```
to:
```cpp
const char* SourceBadgeText(startup::StartupSource source) {
    switch (source) {
        case startup::StartupSource::RegistryHkcuRun:
            return "Registro (tu usuario)";
        case startup::StartupSource::RegistryHklmRun:
            return "Registro (todos los usuarios)";
        case startup::StartupSource::StartupFolderUser:
            return "Carpeta Inicio";
        case startup::StartupSource::StartupFolderCommon:
            return "Carpeta Inicio (todos)";
    }
    return "";
}

} // namespace
```

Then change its one call site:
```cpp
    if (textureId != 0) {
        ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
    } else {
        RenderIconPlaceholder();
    }
```
to:
```cpp
    if (textureId != 0) {
        ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
    } else {
        RenderIconPlaceholder(kIconSize);
    }
```

- [ ] **Step 2: Create `src/ui/GroupsTab.h`**

```cpp
#pragma once
#include "ITab.h"
#include "ConfirmDeleteGroupDialog.h"
#include "GroupEditorDialog.h"
#include "../groups/GroupLauncher.h"
#include "../groups/GroupManager.h"
#include "../groups/GroupProcessTracker.h"
#include "../platform/DX11Renderer.h"
#include "../platform/IconTextureCache.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ui {

// The "Grupos" section: user-defined groups of apps (e.g. a game plus
// Discord plus an overlay) opened/closed together with one click each.
// See docs/superpowers/specs/2026-07-18-grupos-lanzamiento-design.md.
class GroupsTab : public ITab {
public:
    explicit GroupsTab(platform::DX11Renderer& renderer);

    const char* GetTitle() const override { return "Grupos"; }
    const char* GetIcon() const override { return "G"; }
    void OnRender(float deltaTimeSeconds) override;
    void OnTick(uint64_t tickMs) override;

private:
    void RenderGroupCard(const groups::LaunchGroup& group);

    groups::GroupManager manager_;
    groups::GroupProcessTracker tracker_;
    groups::GroupLauncher launcher_{tracker_};
    platform::IconTextureCache iconCache_; // only ever touched from OnRender (render thread)

    ui::GroupEditorDialog editorDialog_;
    ui::ConfirmDeleteGroupDialog confirmDeleteDialog_;

    // Render-thread-only state (button clicks): which groups are
    // currently "open", and the per-entry outcome of each group's last
    // "Abrir grupo" click (for the "ya estaba abierto" note).
    std::unordered_set<std::string> openGroupIds_;
    std::unordered_map<std::string, std::vector<groups::EntryOpenOutcome>> lastOpenOutcomes_;

    std::atomic<uint64_t> tickCounter_{0};

    std::mutex errorMutex_; // guards lastErrorMessage_ (written from OnTick, read from OnRender)
    std::string lastErrorMessage_;
};

} // namespace ui
```

- [ ] **Step 3: Create `src/ui/GroupsTab.cpp`**

```cpp
#include "GroupsTab.h"
#include "CardWidgets.h"
#include "../platform/StringConvert.h"
#include <imgui.h>
#include <mutex>

namespace ui {

namespace {

constexpr float kIconSize = 28.0f;

// Same "filename without extension" idiom already used independently in
// AddStartupEntryDialog.cpp and ShortcutStartupControl.cpp -- small
// enough, and specific enough to this display purpose, that a shared
// helper isn't worth it (see MiniJson for the bar this codebase actually
// uses to justify extracting a shared module: real duplication of
// non-trivial logic, not a three-line path utility).
std::string EntryDisplayName(const groups::LaunchEntry& entry) {
    const std::string& path = entry.resolvedExePath.empty() ? entry.path : entry.resolvedExePath;
    size_t lastSlash = path.find_last_of("\\/");
    std::string fileName = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = fileName.find_last_of('.');
    return (lastDot == std::string::npos) ? fileName : fileName.substr(0, lastDot);
}

} // namespace

GroupsTab::GroupsTab(platform::DX11Renderer& renderer) : iconCache_(renderer) {
    manager_.Init();
}

void GroupsTab::OnTick(uint64_t /*tickMs*/) {
    uint64_t tick = tickCounter_.fetch_add(1) + 1;
    tracker_.Tick(tick);

    std::vector<std::wstring> failures = tracker_.ConsumeCloseFailures();
    if (!failures.empty()) {
        std::string message = "No se pudo cerrar: ";
        for (size_t i = 0; i < failures.size(); ++i) {
            if (i > 0) {
                message += ", ";
            }
            message += platform::WideToUtf8(failures[i]);
        }
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastErrorMessage_ = message;
    }
}

void GroupsTab::OnRender(float /*deltaTimeSeconds*/) {
    if (RenderSectionHeaderAddButton("GRUPOS")) {
        editorDialog_.OpenForCreate();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Snapshot copy, same reasoning as PerfilesTab's customProfilesSnapshot:
    // RenderGroupCard can trigger manager_.DeleteGroup() (via the confirm
    // dialog), which would invalidate a live reference mid-loop.
    std::vector<groups::LaunchGroup> groupsSnapshot = manager_.GetGroups();
    if (groupsSnapshot.empty()) {
        ImGui::TextDisabled("No hay grupos creados todavia.");
    } else {
        for (const groups::LaunchGroup& group : groupsSnapshot) {
            RenderGroupCard(group);
        }
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        if (!lastErrorMessage_.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextDisabled("%s", lastErrorMessage_.c_str());
        }
    }

    editorDialog_.Render(manager_);
    editorDialog_.ConsumeJustSaved(); // groupsSnapshot is re-read from manager_ next frame regardless

    confirmDeleteDialog_.Render(manager_, launcher_, openGroupIds_, tickCounter_.load());
    if (std::optional<std::string> deletedId = confirmDeleteDialog_.ConsumeJustDeleted()) {
        openGroupIds_.erase(*deletedId);
        lastOpenOutcomes_.erase(*deletedId);
    }
}

void GroupsTab::RenderGroupCard(const groups::LaunchGroup& group) {
    ImGui::PushID(group.id.c_str());
    bool isOpenState = openGroupIds_.count(group.id) > 0;

    ImGui::BeginChild("GroupCard", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextUnformatted(group.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(isOpenState ? "(Abierto)" : "(Cerrado)");

    for (const groups::LaunchEntry& entry : group.entries) {
        bool hasTarget = !entry.resolvedExePath.empty();
        uint64_t textureId = hasTarget ? iconCache_.GetOrCreateTexture(entry.resolvedExePath) : 0;
        if (textureId != 0) {
            ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
        } else {
            RenderIconPlaceholder(kIconSize);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    auto outcomeIt = lastOpenOutcomes_.find(group.id);
    if (outcomeIt != lastOpenOutcomes_.end()) {
        for (size_t i = 0; i < outcomeIt->second.size() && i < group.entries.size(); ++i) {
            groups::EntryOpenStatus status = outcomeIt->second[i].status;
            if (status == groups::EntryOpenStatus::ExternallyOwned) {
                ImGui::TextDisabled("%s ya estaba abierto -- no se cerrara con este grupo.",
                                     EntryDisplayName(group.entries[i]).c_str());
            } else if (status == groups::EntryOpenStatus::LaunchFailed) {
                // Per-entry visibility for a failed launch (broken .lnk,
                // moved/deleted exe, permissions) -- matches the design
                // spec's error matrix: a failure is shown, never silently
                // swallowed, without aborting the rest of the group.
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No se pudo abrir %s.",
                                    EntryDisplayName(group.entries[i]).c_str());
            }
        }
    }

    ImGui::BeginDisabled(isOpenState);
    if (ImGui::Button("Abrir grupo")) {
        std::vector<groups::EntryOpenOutcome> outcomes = launcher_.OpenGroup(group.id, group.entries);
        bool anyFailed = false;
        for (const groups::EntryOpenOutcome& outcome : outcomes) {
            if (outcome.status == groups::EntryOpenStatus::LaunchFailed) {
                anyFailed = true;
            }
        }
        lastOpenOutcomes_[group.id] = std::move(outcomes);
        openGroupIds_.insert(group.id);

        std::lock_guard<std::mutex> lock(errorMutex_);
        lastErrorMessage_ = anyFailed ? ("No se pudieron abrir todas las apps de \"" + group.name + "\".") : "";
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!isOpenState);
    if (ImGui::Button("Cerrar grupo")) {
        launcher_.CloseGroup(group.id, group.entries, tickCounter_.load());
        openGroupIds_.erase(group.id);
        lastOpenOutcomes_.erase(group.id);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Editar")) {
        editorDialog_.OpenForEdit(group);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Eliminar")) {
        confirmDeleteDialog_.OpenForGroup(group);
    }

    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PopID();
}

} // namespace ui
```

- [ ] **Step 4: Register the tab in `src/app/Application.cpp`**

Change the includes block at the top:
```cpp
#include "Application.h"
#include "AutoStart.h"
#include "../ui/HardwareMonitorTab.h"
#include "../ui/PerfilesTab.h"
#include "../ui/StartupTab.h"
#include "../ui/Theme.h"
```
to:
```cpp
#include "Application.h"
#include "AutoStart.h"
#include "../ui/GroupsTab.h"
#include "../ui/HardwareMonitorTab.h"
#include "../ui/PerfilesTab.h"
#include "../ui/StartupTab.h"
#include "../ui/Theme.h"
```

Then change:
```cpp
    tabManager_.RegisterTab(std::make_unique<ui::StartupTab>(hwnd_, renderer_));
```
to:
```cpp
    tabManager_.RegisterTab(std::make_unique<ui::StartupTab>(hwnd_, renderer_));
    tabManager_.RegisterTab(std::make_unique<ui::GroupsTab>(renderer_));
```

- [ ] **Step 5: Add to `CMakeLists.txt`**

Change:
```cmake
    src/ui/ConfirmDeleteGroupDialog.cpp
    src/ui/AddStartupEntryDialog.cpp
```
to:
```cmake
    src/ui/ConfirmDeleteGroupDialog.cpp
    src/ui/GroupsTab.cpp
    src/ui/AddStartupEntryDialog.cpp
```

- [ ] **Step 6: Build and do a real launch smoke check**

Run: `cmake --build build --target MkPCApp --config Debug`
Expected: build succeeds with 0 errors.

Then, from the repo root (this environment has direct access to the built Windows executable):
```bash
build/bin/MkPCApp.exe &
sleep 3
```
Check the process is still alive (didn't crash on startup) and exit cleanly:
```powershell
Get-Process MkPCApp -ErrorAction SilentlyContinue
```
Expected: the process is listed and running. If the user has an existing instance running from a previous session, stop it first (see the icon feature's earlier precedent in this same session) before launching the freshly built one, otherwise the single-instance mutex just brings the old window to front instead of testing the new build.

Stop the test instance afterward:
```powershell
Stop-Process -Name MkPCApp -Force -ErrorAction SilentlyContinue
```

- [ ] **Step 7: Commit**

```bash
git add src/ui/CardWidgets.h src/ui/CardWidgets.cpp src/ui/StartupTab.cpp src/ui/GroupsTab.h src/ui/GroupsTab.cpp src/app/Application.cpp CMakeLists.txt
git commit -m "Add ui::GroupsTab and register it as a new sidebar section"
```

---

### Task 12: Update `docs/PROJECT_STATUS.md` / `docs/ARCHITECTURE.md`, manual QA checklist

**Files:**
- Modify: `docs/PROJECT_STATUS.md`
- Modify: `docs/ARCHITECTURE.md`

- [ ] **Step 1: Add a new iteration section to `docs/PROJECT_STATUS.md`**

Insert a new section right after the maintenance-rule section and before the current top iteration (`## Iteración 3 — ...`), following the exact structure of the existing iteration sections (Qué hace / Limitaciones conocidas / Arquitectura / Verificación pendiente):

```markdown
## Iteración 4 — Sección "Grupos": abrir/cerrar conjuntos de apps de un clic (en desarrollo, rama `feature/gestor-inicio`)

Añade una cuarta sección (`ui::GroupsTab`, registrada junto a
`HardwareMonitorTab`/`PerfilesTab`/`StartupTab` sin tocar el resto del
shell) para definir grupos de apps asociadas (p. ej. un juego + Discord +
un overlay) y abrirlas/cerrarlas todas con un clic cada una.

### Qué hace

- **Grupos ilimitados**, cada uno con nombre y una lista de apps
  (cualquier `.exe`, acceso directo `.lnk`, u otro ejecutable -- sin
  filtro de extensión en el selector de archivos, para cubrir casos como
  Minecraft que arranca vía Java).
- **Dos botones por grupo**, ambos manuales: "Abrir grupo" (lanza cada
  app que no esté ya corriendo) y "Cerrar grupo" (cierra solo lo que este
  grupo abrió). No hay detección automática de que el juego se cerró.
- **Apps compartidas entre grupos abiertos a la vez nunca se cierran de
  más**: si dos grupos abiertos usan la misma app (p. ej. Discord en
  "League of Legends" y en "Minecraft"), cerrar uno de los dos la deja
  corriendo -- solo se cierra cuando el último grupo que la reclama
  también se cierra. Una app que el usuario ya tenía abierta antes de
  pulsar "Abrir grupo" nunca se toca.
- **Cierre amable, con margen**: al cerrar, se manda `WM_CLOSE` a las
  ventanas de la app y se espera unos segundos antes de forzar el cierre
  (`TerminateProcess`) si sigue viva.
- **Persistencia** de la lista de grupos (nombre + apps, nunca estado de
  ejecución) en `%LOCALAPPDATA%\MkPCApp\groups.json`, formato propio vía
  el mismo parser JSON minimalista que ya usa "Perfiles"
  (`platform::MiniJson`, extraído de `ProfileJson` en esta misma
  iteración para no duplicarlo).

### Limitaciones conocidas / mejor esfuerzo

- **El estado "abierto/cerrado" vive solo en memoria**: si `MkPCApp.exe`
  se reinicia mientras un grupo está "abierto", las apps que lanzó siguen
  corriendo (nunca se cierran solas al salir de MkPCApp), pero la app ya
  no recuerda que ese grupo estaba abierto -- hay que pulsar "Abrir" de
  nuevo para que el conteo de propietarios vuelva a ser correcto.
- **Sin Tareas Programadas ni URIs de launcher** (`steam://...`, etc.) en
  esta primera iteración -- solo rutas de archivo (`.exe`/`.lnk`/
  cualquier ejecutable) y argumentos de línea de comandos opcionales.

### Arquitectura (resumen)

Módulo `src/groups/` (tipos puros en `GroupTypes.h`, persistencia en
`GroupStore`/`GroupJson`, CRUD en memoria en `GroupManager`, primitivas
Win32 de proceso en `ProcessLifecycle`, conteo de referencias entre
grupos en `GroupProcessTracker`, orquestación de abrir/cerrar en
`GroupLauncher`) más `src/ui/GroupsTab` (+ `GroupEditorDialog`,
`ConfirmDeleteGroupDialog`), siguiendo el mismo patrón de extensión por
`ITab` que ya usan Hardware Monitor, Perfiles e Inicio. El conteo de
referencias por ruta de ejecutable es lo único sin equivalente previo en
la app -- ver `docs/ARCHITECTURE.md` para el detalle de por qué un grupo
se une como copropietario en vez de relanzar cuando otro grupo abierto ya
tiene la app corriendo.

### Verificación pendiente (requiere máquina Windows real)

Confirmado: compila y el ejecutable arranca sin errores. Aún sin probar
en la máquina real del usuario: crear un grupo con varias apps y pulsar
"Abrir grupo" las lanza todas; "Cerrar grupo" cierra solo lo que ese
grupo abrió; una app compartida entre dos grupos abiertos sobrevive al
cierre de uno de los dos y se cierra al cerrar el segundo; una app que el
usuario ya tenía abierta antes de "Abrir grupo" nunca se cierra desde la
app; el cierre amable le da tiempo a una app real a cerrarse sola antes
de forzar; eliminar un grupo que estaba abierto cierra sus apps primero.
```

- [ ] **Step 2: Add an architecture section to `docs/ARCHITECTURE.md`**

Insert a new `## Groups module (`src/groups/`)` section after the existing `## Startup module (`src/startup/`)` section (at the end of the file), explaining the shared-ownership decision at the architecture level:

```markdown
## Groups module (`src/groups/`)

The "Grupos" tab (`ui::GroupsTab`) is the fourth section built on the
`ITab` extension point above. Same split-responsibility shape as
`src/profiles/`/`src/startup/`: `groups::GroupManager` owns the persisted
list of groups (CRUD only, no process state), `groups::ProcessLifecycle`
is a set of pure Win32 free functions (spawn, check-running,
graceful-close-request, force-terminate) with no app-specific state, and
`groups::GroupProcessTracker` + `groups::GroupLauncher` do the one thing
with no earlier precedent in this app: deciding whether opening a group
should launch a process, join an existing one, or leave a process alone
entirely.

**Why "is this path owned by another open group" is checked before "is
it running at all".** A naive "already running -> never touch it" check
breaks the explicit requirement that two currently-open groups can share
an app (e.g. Discord used by both "League of Legends" and "Minecraft"):
if group A launches Discord and group B opens later while it's still
running, B must join as a co-owner, not treat it as untouchable -- only a
process running for a reason `GroupProcessTracker` doesn't recognize (in
practice: the user opened it by hand) gets the "leave it alone entirely"
treatment. `GroupLauncher::OpenGroup` checks
`GroupProcessTracker::IsPathCurrentlyOwned` first for exactly this
reason; see `docs/superpowers/specs/2026-07-18-grupos-lanzamiento-design.md`
for the full walkthrough (including the correction made to the original
spec once this distinction was discovered while writing the
implementation plan).

**Why closing is "graceful, then forced" instead of an immediate
`TerminateProcess`.** Windows has no generic "please close nicely" API --
`GroupProcessTracker::ReleaseOwnership` sends `WM_CLOSE` to the process's
own top-level windows (via `EnumWindows` filtered by owning PID) the
moment the last owning group releases it, then schedules a
`TerminateProcess` fallback a few ticks later (`GroupProcessTracker::Tick`,
driven by `GroupsTab::OnTick`, the same 1 Hz background thread every
other tab's periodic work already runs on) in case the app didn't close
itself in time.

**In-memory only, by design.** Unlike `groups::GroupManager`'s persisted
group definitions, `GroupProcessTracker`'s ownership map and pending-close
queue live only for the current `MkPCApp.exe` session -- see
`docs/PROJECT_STATUS.md` for the accepted limitation this implies after a
restart. Its destructor closes (but never terminates) any process handles
still tracked at shutdown, so exiting MkPCApp never kills an app a group
launched.
```

- [ ] **Step 3: Commit**

```bash
git add docs/PROJECT_STATUS.md docs/ARCHITECTURE.md
git commit -m "Document Grupos feature in PROJECT_STATUS.md and ARCHITECTURE.md"
```

- [ ] **Step 4: Manual QA checklist for the user (real Windows machine, GUI click-through)**

This is the one part of the feature that cannot be verified by building or by this plan's automated steps alone (no GUI automation tool is available for the native ImGui window in this environment). Hand this checklist to the user (or perform it interactively) before considering the feature done:

1. Launch `MkPCApp.exe`, open the new "Grupos" tab (sidebar icon "G").
2. Click "+", create a group named "Test" with two apps you can tell apart at a glance (e.g. Notepad and Calculator via their `.exe` paths). Save.
3. Click "Abrir grupo" -- both apps should open. The card should show "(Abierto)" and both buttons should reflect that state (Abrir disabled, Cerrar enabled).
4. Click "Cerrar grupo" -- both apps should close within a few seconds (graceful close). The card should return to "(Cerrado)".
5. Create a second group "Test2" containing one of the same two apps (e.g. just Notepad) plus a third app.
6. Open "Test" (opens Notepad + Calculator), then open "Test2" (Notepad already running should NOT relaunch, but should not be marked "externally owned" either -- no "ya estaba abierto" note should appear for it).
7. Close "Test" -- Notepad should stay open (still owned by "Test2"), Calculator should close.
8. Close "Test2" -- Notepad should now close.
9. Open Notepad manually (outside the app) first, then click "Abrir grupo" on a group that includes it -- the card should show a "ya estaba abierto -- no se cerrara con este grupo" note for it, and closing that group should leave your manually-opened Notepad running.
10. Edit a group (add/remove an app, rename it), confirm it persists after restarting MkPCApp.
11. Delete a group while it's open -- its apps should close as part of the deletion.
12. Restart MkPCApp with a group's apps still running from a previous "Abrir grupo" -- confirm the card shows "(Cerrado)" (expected per the documented in-memory-only limitation) and that the apps themselves are untouched (still running).
