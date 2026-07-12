#pragma once
#include "../ProfileTypes.h"
#include <optional>

namespace profiles {
namespace Volume {

ApplyResult SetVolume(int percent);
std::optional<int> GetVolume();

} // namespace Volume
} // namespace profiles
