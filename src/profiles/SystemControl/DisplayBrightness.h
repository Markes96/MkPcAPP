#pragma once
#include "../ProfileTypes.h"
#include <optional>

namespace profiles {
namespace DisplayBrightness {

// Unified brightness — deliberately never distinguishes internal (laptop
// panel) vs external monitor in the public API. Internally tries both a WMI
// path (internal panel) and DDC/CI physical-monitor path (external), and
// treats "at least one surface responded" as success, mirroring the
// profile-level "don't block on one failing variable" philosophy at the
// per-surface level too.
ApplyResult SetBrightness(int percent);
std::optional<int> GetBrightness();

} // namespace DisplayBrightness
} // namespace profiles
