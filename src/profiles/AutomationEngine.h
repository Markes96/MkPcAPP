#pragma once
#include "ProfileTypes.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace profiles {

class ProfileManager;

// Owns the ordered list of automation rules (loaded/persisted via
// ProfileStore, same profiles.json file as ProfileManager's custom
// profiles) and evaluates them against the current moment/system state.
// Never touches Win32/WMI/COM itself for anything other than reading the
// clock/battery status to evaluate rule conditions -- applying a matched
// rule's profile goes through ProfileManager::ApplyProfile(), same as a
// manual click from the UI.
//
// rules_/lastKnownAcLineStatus_ are guarded by mutex_: Tick() runs on the
// background data-tick thread once per second, while rule CRUD (from
// PerfilesTab/RuleEditorDialog) and OnPowerSourceChangeHint() (from
// Application::OnPowerBroadcast, dispatched on the UI thread) both run on
// the UI thread -- the same cross-thread pattern ProfileManager guards.
class AutomationEngine {
public:
    void Init(ProfileManager& profileManager);

    std::vector<AutomationRule> GetRules() const;
    std::string AddRule(const AutomationRule& rule);
    bool UpdateRule(const std::string& id, const AutomationRule& rule);
    bool DeleteRule(const std::string& id);
    void MoveRule(size_t fromIndex, size_t toIndex);

    // Evaluates rules top-to-bottom, applying the first ENABLED rule whose
    // condition currently holds, via profileManager_->ApplyProfile() -- but
    // only if that rule's target isn't already the active profile, UNLESS
    // it's been more than kForceReapplyIntervalMs since the last real apply
    // (see .cpp): a level condition that stays true for a long time, e.g. an
    // overnight time window, would otherwise re-run the full apply pipeline
    // every tick (wasteful), but never re-applying at all would mean any
    // drift from an external change (the user, Windows, or another app
    // touching the same settings) never gets corrected for as long as the
    // condition holds -- the periodic force-reapply is what still catches
    // that drift, just at a far cheaper cadence than every tick. A rule
    // whose targetProfileId no longer matches any known profile (e.g. it was
    // deleted) is skipped rather than counted as fired, so
    // ReconcileOnStartup's live-state fallback still gets a chance to run.
    // Returns true if a rule with a real target matched. Always updates
    // lastKnownAcLineStatus_ as a side effect (see .cpp for why that
    // tracking is unconditional).
    bool EvaluateNow();

    void Tick();
    void OnPowerSourceChangeHint();

private:
    static std::string GenerateRuleId();
    void Save(); // caller must already hold mutex_

    ProfileManager* profileManager_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<AutomationRule> rules_;
    std::optional<BYTE> lastKnownAcLineStatus_;
    // GetTickCount64() timestamp of the last real ApplyProfile() call made
    // from EvaluateNow(); std::atomic since Tick() (background thread) and
    // OnPowerSourceChangeHint() (UI thread) can both read/write it without
    // needing the full rules_ lock for just this one counter.
    std::atomic<ULONGLONG> lastAppliedTickMs_{0};
};

} // namespace profiles
