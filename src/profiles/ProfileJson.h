#pragma once
// Hand-rolled, schema-specific JSON reader/writer for profiles.json.
//
// This is deliberately NOT a general-purpose JSON library: vendoring
// nlohmann/json's ~25,000-line single header just to read/write a handful of
// flat fields (a "profiles" array of int/bool/string properties) is a bad
// trade for this app. What's here only needs to be correct for our fixed
// shape: {"version": <int>, "profiles": [ {...}, ... ], "rules": [ {...}, ... ]}.
// The "rules" array (automation) was added alongside "profiles", same approach.
#include "ProfileTypes.h"
#include <string>
#include <vector>

namespace profiles {

std::string SerializeProfiles(const std::vector<Profile>& profiles);

// Parses `jsonText` and replaces outProfiles with the parsed list. Returns
// false on any malformed/unexpected input (outProfiles left untouched) —
// callers should treat that as "no custom profiles" rather than crash.
bool DeserializeProfiles(const std::string& jsonText, std::vector<Profile>& outProfiles);

// Same shape/idiom as the profile (de)serialization above, for the "rules"
// top-level array. Standalone -- produces/reads a document containing only
// "version"/"rules" (no "profiles" key). ProfileStore doesn't use this pair
// directly for its combined on-disk file (see SerializeDocument below); kept
// for symmetry with SerializeProfiles/DeserializeProfiles and for anything
// that only ever cares about rules in isolation.
std::string SerializeRules(const std::vector<AutomationRule>& rules);

// Parses `jsonText` and replaces outRules with the parsed list. Returns false
// only on malformed/unexpected input (outRules left untouched). A missing
// "rules" array (e.g. a profiles.json written before this phase existed) is
// NOT an error -- outRules is set to empty and this returns true, since
// absent just means zero rules were ever configured.
bool DeserializeRules(const std::string& jsonText, std::vector<AutomationRule>& outRules);

// The actual on-disk document shape ProfileStore reads/writes:
// {"version": 1, "profiles": [...], "rules": [...]}. Both arrays live in the
// same file, so writing one must not clobber the other -- ProfileStore reads
// the currently-persisted counterpart before calling this to write the
// merged result.
std::string SerializeDocument(const std::vector<Profile>& profiles, const std::vector<AutomationRule>& rules);

} // namespace profiles
