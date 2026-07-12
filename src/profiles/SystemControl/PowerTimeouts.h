#pragma once
#include "../ProfileTypes.h"
#include <optional>

namespace profiles {
namespace PowerTimeouts {

// A timeout of 0 seconds means "never", matching Windows' own power-timeout
// semantics — passed straight through to PowerWriteACValueIndex/DC without
// special-casing.
ApplyResult SetScreenOffTimeouts(int acSeconds, int dcSeconds);
ApplyResult SetSleepTimeouts(int acSeconds, int dcSeconds);
ApplyResult SetHibernateTimeouts(int acSeconds, int dcSeconds);

std::optional<int> GetScreenOffTimeoutAc();
std::optional<int> GetScreenOffTimeoutDc();
std::optional<int> GetSleepTimeoutAc();
std::optional<int> GetSleepTimeoutDc();
std::optional<int> GetHibernateTimeoutAc();
std::optional<int> GetHibernateTimeoutDc();

} // namespace PowerTimeouts
} // namespace profiles
