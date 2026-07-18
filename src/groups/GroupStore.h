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
