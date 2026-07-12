#include "AutomationEngine.h"
#include "ProfileManager.h"
#include "ProfileStore.h"
#include <combaseapi.h>
#include <algorithm>
#include <cstdio>

namespace profiles {

namespace {

// How long a level-condition rule (TimeWindow/BatteryBelow) can stay matched
// without a real re-apply before EvaluateNow() forces one anyway, to catch
// drift from the live system settings being changed outside MkPCApp (by the
// user, Windows itself, or another app) while the condition holds for a long
// time (e.g. an overnight window). Deliberately much cheaper than the old
// every-tick re-apply this replaced, while still self-healing eventually
// instead of never.
constexpr ULONGLONG kForceReapplyIntervalMs = 5ULL * 60ULL * 1000ULL;

enum class PowerTransition { None, AcToBattery, BatteryToAc };

bool TimeWindowConditionHolds(const AutomationRule& rule, const SYSTEMTIME& localTime) {
    int minuteOfDay = localTime.wHour * 60 + localTime.wMinute;
    int start = rule.startMinuteOfDay;
    int end = rule.endMinuteOfDay;

    bool inWindow = (end >= start) ? (minuteOfDay >= start && minuteOfDay < end)
                                    : (minuteOfDay >= start || minuteOfDay < end);
    if (!inWindow) {
        return false;
    }

    if (rule.weekdaysOnly) {
        // SYSTEMTIME::wDayOfWeek is 0=Sunday..6=Saturday; Monday-Friday is 1-5.
        if (localTime.wDayOfWeek < 1 || localTime.wDayOfWeek > 5) {
            return false;
        }
    }
    return true;
}

bool BatteryBelowConditionHolds(const AutomationRule& rule, const SYSTEM_POWER_STATUS& status) {
    if (status.BatteryLifePercent == 255) {
        return false; // unknown
    }
    return status.BatteryLifePercent < rule.batteryThresholdPercent;
}

bool PowerSourceChangeConditionHolds(const AutomationRule& rule, PowerTransition transition) {
    if (transition == PowerTransition::None) {
        return false;
    }
    PowerTransition expected = rule.triggerOnAcToBattery ? PowerTransition::AcToBattery : PowerTransition::BatteryToAc;
    return transition == expected;
}

} // namespace

void AutomationEngine::Init(ProfileManager& profileManager) {
    profileManager_ = &profileManager;
    rules_ = ProfileStore::LoadRules();
}

std::vector<AutomationRule> AutomationEngine::GetRules() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rules_;
}

std::string AutomationEngine::GenerateRuleId() {
    GUID guid;
    if (FAILED(CoCreateGuid(&guid))) {
        return "rule.fallback";
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "rule.%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
                  guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return std::string(buffer);
}

void AutomationEngine::Save() {
    ProfileStore::SaveRules(rules_);
}

std::string AutomationEngine::AddRule(const AutomationRule& rule) {
    AutomationRule r = rule;
    r.id = GenerateRuleId();
    std::lock_guard<std::mutex> lock(mutex_);
    rules_.push_back(r);
    Save();
    return r.id;
}

bool AutomationEngine::UpdateRule(const std::string& id, const AutomationRule& rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (AutomationRule& existing : rules_) {
        if (existing.id != id) {
            continue;
        }
        AutomationRule updated = rule;
        updated.id = id; // id is never mutated through this path
        existing = updated;
        Save();
        return true;
    }
    return false;
}

bool AutomationEngine::DeleteRule(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(rules_.begin(), rules_.end(),
                            [&](const AutomationRule& r) { return r.id == id; });
    if (it == rules_.end()) {
        return false;
    }
    rules_.erase(it);
    Save();
    return true;
}

void AutomationEngine::MoveRule(size_t fromIndex, size_t toIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fromIndex == toIndex || fromIndex >= rules_.size() || toIndex >= rules_.size()) {
        return;
    }
    AutomationRule moved = rules_[fromIndex];
    rules_.erase(rules_.begin() + static_cast<ptrdiff_t>(fromIndex));
    rules_.insert(rules_.begin() + static_cast<ptrdiff_t>(toIndex), moved);
    Save();
}

bool AutomationEngine::EvaluateNow() {
    SYSTEMTIME localTime;
    GetLocalTime(&localTime);

    SYSTEM_POWER_STATUS powerStatus;
    bool havePowerStatus = GetSystemPowerStatus(&powerStatus) != 0;

    // Determine whether a power-source transition happened since the last
    // check. A PowerSourceChange rule is a one-shot pulse: it only "fires" in
    // the exact EvaluateNow() call where the transition is detected, not for
    // as long as the system stays on battery/AC afterwards -- unlike
    // TimeWindow/BatteryBelow, which stay true for as long as the underlying
    // condition holds. That means a TimeWindow rule that's also true at the
    // same moment can still win on a later tick even if a PowerSourceChange
    // rule won this particular one, since the pulse stops matching right
    // after this call. This is intentional priority-ordering behavior (the
    // user controls rule order), not a bug to work around here.
    PowerTransition transition = PowerTransition::None;

    // rules_ is snapshotted under the lock so the loop below (which calls
    // into ProfileManager::ApplyProfile, real Win32/WMI/COM I/O that can be
    // slow) never runs while holding mutex_ -- Tick() (background thread)
    // and rule CRUD/OnPowerSourceChangeHint() (UI thread) would otherwise
    // block each other for the duration of an apply.
    std::vector<AutomationRule> rulesSnapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (havePowerStatus && powerStatus.ACLineStatus != 255) {
            if (lastKnownAcLineStatus_.has_value()) {
                BYTE previous = *lastKnownAcLineStatus_;
                BYTE current = powerStatus.ACLineStatus;
                if (previous != current) {
                    // ACLineStatus: 0 = offline (battery), 1 = online (AC).
                    transition = (current == 0) ? PowerTransition::AcToBattery : PowerTransition::BatteryToAc;
                }
            }
            // First-ever check: just learn the current source, no transition
            // to report -- ReconcileOnStartup's own detection already covers
            // "what should be active right now" without treating startup as
            // a pulse.
            lastKnownAcLineStatus_ = powerStatus.ACLineStatus;
        }
        rulesSnapshot = rules_;
    }

    bool fired = false;
    for (const AutomationRule& rule : rulesSnapshot) {
        if (!rule.enabled) {
            continue;
        }

        bool conditionHolds = false;
        switch (rule.type) {
            case RuleTriggerType::TimeWindow:
                conditionHolds = TimeWindowConditionHolds(rule, localTime);
                break;
            case RuleTriggerType::BatteryBelow:
                conditionHolds = havePowerStatus && BatteryBelowConditionHolds(rule, powerStatus);
                break;
            case RuleTriggerType::PowerSourceChange:
                conditionHolds = PowerSourceChangeConditionHolds(rule, transition);
                break;
        }

        if (!conditionHolds) {
            continue;
        }

        if (!profileManager_ || !profileManager_->ProfileExists(rule.targetProfileId)) {
            // Dead reference (e.g. the target profile was since deleted) --
            // don't report this as "fired": that would make
            // ReconcileOnStartup skip its live-state detection fallback for
            // no real profile, and would block a lower-priority rule that
            // does point at something real. Keep looking.
            continue;
        }

        // Re-apply if this isn't already the active profile, OR if it's
        // been long enough since the last real apply that live settings
        // could have drifted (see kForceReapplyIntervalMs) -- balances not
        // re-running the full apply pipeline every tick against still
        // self-correcting drift eventually rather than never.
        bool alreadyActive = profileManager_->GetActiveProfileId() == rule.targetProfileId;
        ULONGLONG now = GetTickCount64();
        bool dueForPeriodicReapply = (now - lastAppliedTickMs_.load()) >= kForceReapplyIntervalMs;
        if (!alreadyActive || dueForPeriodicReapply) {
            profileManager_->ApplyProfile(rule.targetProfileId);
            lastAppliedTickMs_.store(now);
        }
        fired = true;
        break;
    }

    return fired;
}

void AutomationEngine::Tick() {
    EvaluateNow();
}

void AutomationEngine::OnPowerSourceChangeHint() {
    EvaluateNow();
}

} // namespace profiles
