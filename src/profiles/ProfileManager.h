#pragma once
#include "ProfileTypes.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace profiles {

class AutomationEngine;

// Owns the full set of profiles (5 fixed predefined + user-created custom
// ones, loaded via ProfileStore) and which one is currently considered
// active. The only thing that touches Win32/WMI/COM APIs to actually change
// system state is the profiles::*  SystemControl functions this class calls
// into — UI code (ui::PerfilesTab) never calls those directly.
//
// Instances are shared between the main/UI thread (every ImGui frame, plus
// profile CRUD from PerfilesTab/ProfileEditorDialog) and the background
// data-tick thread (AutomationEngine::Tick(), once per second, calls back
// into ApplyProfile()) -- mutex_ guards every field below the same way
// SensorAggregator guards its own cross-thread state. Predefined profiles
// are the one exception: predefinedProfiles_ is built once in Init(), before
// the tick thread is started (see Application::Init), and never mutated
// again, so reading it without the lock afterwards is safe.
class ProfileManager {
public:
    // Loads predefined + custom profiles only -- does NOT reconcile the
    // active profile. Callers that also have an AutomationEngine (i.e.
    // app::Application) should call this, then init/construct that engine,
    // then call ReconcileOnStartup(&engine) explicitly, so rule evaluation
    // can run ahead of the live-state detection fallback. Callers without an
    // AutomationEngine can call Init() followed by ReconcileOnStartup(nullptr).
    // Must be called before any other thread touches this instance.
    void Init();

    const std::vector<Profile>& GetPredefinedProfiles() const { return predefinedProfiles_; }
    std::vector<Profile> GetCustomProfiles() const;
    std::optional<std::string> GetActiveProfileId() const;

    // True if profileId matches a known predefined or custom profile. Used
    // by AutomationEngine to tell a live profile apart from a rule that
    // still targets one that's since been deleted.
    bool ProfileExists(const std::string& profileId) const;

    // Returns a copy of the named profile (predefined or custom), or
    // std::nullopt if profileId doesn't match any. Used by
    // ProfileEditorDialog to seed a new profile's defaults from a real
    // predefined profile instead of a second hardcoded copy of its values.
    std::optional<Profile> GetProfileById(const std::string& profileId) const;

    // Applies every always-on variable (power plan, timeouts, brightness) and
    // every Overridable variable with apply==true for the given profile
    // (predefined or custom). Never blocks the rest of a profile over one
    // failing variable — collects one AppliedVariableResult per attempted
    // variable and always marks the profile active on completion, even if
    // some variables failed/were unsupported (a partially-applied profile is
    // still "the one you asked for"). Returns an empty vector if profileId
    // doesn't match any known profile (no-op, activeProfileId_ unchanged).
    std::vector<AppliedVariableResult> ApplyProfile(const std::string& profileId);

    // Reads live Windows state and compares it against every known profile
    // (predefined first in fixed order, then custom in list order), setting
    // activeProfileId_ to the first exact match or std::nullopt if none
    // match. Brightness compares with +/-2 tolerance. Any Overridable field
    // with apply==false in a candidate profile is skipped for that
    // comparison. Best-effort variables that can't currently be read are
    // skipped in the comparison for every profile rather than counted as a
    // mismatch.
    void DetectCurrentProfile();

    // Creates a new custom profile with a freshly generated id, appends it to
    // customProfiles_, persists via ProfileStore::Save, and returns the new
    // id. Never touches predefinedProfiles_.
    std::string CreateProfile(const std::string& name, const std::string& icon,
                               const ProfileVariables& vars);

    // Updates an existing custom profile in place (name/icon/vars) and
    // persists. Returns false (no-op) if id isn't found in customProfiles_ or
    // belongs to a predefined profile instead — predefined profiles are never
    // mutated through this path. Does not re-apply the profile even if it's
    // currently active; editing and applying are separate actions.
    bool UpdateProfile(const std::string& id, const std::string& name, const std::string& icon,
                        const ProfileVariables& vars);

    // Removes a custom profile and persists. Returns false if id isn't found
    // in customProfiles_ or belongs to a predefined profile. If the deleted
    // profile was active, activeProfileId_ becomes std::nullopt rather than
    // silently pointing at a profile that no longer exists (and nothing else
    // is auto-applied in its place).
    bool DeleteProfile(const std::string& id);

    // Startup reconciliation (see design doc): if automationEngine is
    // non-null and its EvaluateNow() finds a matching rule, that rule's
    // ApplyProfile() call already set activeProfileId_ -- return immediately
    // without touching live Windows state. Otherwise (nullptr, or no rule
    // matched) fall through to DetectCurrentProfile() as before.
    void ReconcileOnStartup(AutomationEngine* automationEngine);

private:
    // Callers must already hold mutex_ (or be in Init(), before any other
    // thread exists) when calling this.
    const Profile* FindProfileByIdLocked(const std::string& profileId) const;
    // Shared tail of ApplyProfile/DetectCurrentProfile/DeleteProfile so the
    // three don't each hand-write their own "this is now the active
    // profile" assignment. Caller must already hold mutex_.
    void SetActiveProfileIdLocked(std::optional<std::string> id);
    static std::vector<Profile> BuildPredefinedProfiles();
    static std::string GenerateCustomProfileId();

    mutable std::mutex mutex_;
    std::vector<Profile> predefinedProfiles_;
    std::vector<Profile> customProfiles_;
    std::optional<std::string> activeProfileId_;
};

} // namespace profiles
