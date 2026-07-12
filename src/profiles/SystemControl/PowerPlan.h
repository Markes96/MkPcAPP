#pragma once
#include "../ProfileTypes.h"
#include <optional>

namespace profiles {

// Named PowerPlanControl (not PowerPlan) purely to avoid colliding with the
// profiles::PowerPlan enum in ProfileTypes.h — a namespace and a type can't
// share a name in the same enclosing namespace.
namespace PowerPlanControl {

ApplyResult SetActiveScheme(PowerPlan plan);
std::optional<PowerPlan> GetActiveScheme();

} // namespace PowerPlanControl

} // namespace profiles
