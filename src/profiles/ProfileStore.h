#pragma once
#include "ProfileTypes.h"
#include <vector>

namespace profiles {

// Persists custom (non-predefined) profiles to
// %LOCALAPPDATA%\MkPCApp\profiles.json. Predefined profiles are never
// persisted here — they're rebuilt in code on every launch.
namespace ProfileStore {

// Never throws. Returns an empty list on any missing-file/read/parse failure.
std::vector<Profile> Load();

// Full-file overwrite — this data changes rarely (profile create/edit/delete),
// no need for atomic-write/temp-file complexity. Preserves whatever
// automation rules are already on disk (reads them first) so saving profiles
// never clobbers rules living in the same file.
void Save(const std::vector<Profile>& customProfiles);

// Never throws. Returns an empty list on any missing-file/read/parse failure,
// and also on a well-formed file that simply predates automation rules (no
// "rules" key) -- that's zero rules, not an error.
std::vector<AutomationRule> LoadRules();

// Full-file overwrite, same idiom as Save() above but for the "rules" array
// — preserves whatever custom profiles are already on disk.
void SaveRules(const std::vector<AutomationRule>& rules);

} // namespace ProfileStore

} // namespace profiles
