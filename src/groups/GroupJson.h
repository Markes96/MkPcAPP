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
