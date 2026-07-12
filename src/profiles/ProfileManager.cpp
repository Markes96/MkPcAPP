#include "ProfileManager.h"
#include "AutomationEngine.h"
#include "ProfileStore.h"
#include "SystemControl/PowerPlan.h"
#include "SystemControl/PowerTimeouts.h"
#include "SystemControl/DisplayBrightness.h"
#include "SystemControl/Volume.h"
#include <windows.h>
#include <combaseapi.h>
#include <algorithm>
#include <cstdio>

namespace profiles {

namespace {

// Brackets a single ApplyProfile/DetectCurrentProfile call with exactly one
// CoInitializeEx/CoUninitialize pair, instead of each SystemControl function
// doing its own — those are called several times per Apply/Detect and
// shouldn't repeatedly init/uninit COM on the same thread.
class ComScope {
public:
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_FALSE means COM was already initialized on this thread (by us or
        // someone else) in a compatible mode — still must be paired with a
        // CoUninitialize call. RPC_E_CHANGED_MODE means a different
        // concurrency model is already set on this thread (e.g. by ImGui/DX11
        // internals) — our call didn't take effect, so don't uninitialize.
        shouldUninitialize_ = (hr == S_OK || hr == S_FALSE);
    }

    ~ComScope() {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool shouldUninitialize_ = false;
};

// Shared by ApplyProfile/DetectCurrentProfile for Overridable<T> variables
// (currently just volume), which follow the "only touch it if apply==true"
// shape -- keeping this in one place means that shape can't drift between
// the apply side and the detect side. The
// always-on variables (power plan, timeouts, brightness) don't fit this
// template (different arities/tolerances) and are still hand-written below;
// adding a new always-on variable still means updating ApplyProfile,
// DetectCurrentProfile's matches lambda, and BuildPredefinedProfiles by hand.
template <typename T>
void ApplyOverridable(std::vector<AppliedVariableResult>& results, const char* name,
                       const Overridable<T>& field, ApplyResult (*setter)(T)) {
    if (field.apply) {
        results.push_back({name, setter(field.value)});
    }
}

template <typename T>
bool OverridableMismatches(const Overridable<T>& field, const std::optional<T>& current) {
    return field.apply && current.has_value() && current.value() != field.value;
}

} // namespace

void ProfileManager::Init() {
    predefinedProfiles_ = BuildPredefinedProfiles();
    customProfiles_ = ProfileStore::Load();
}

void ProfileManager::ReconcileOnStartup(AutomationEngine* automationEngine) {
    if (automationEngine && automationEngine->EvaluateNow()) {
        // A rule fired and its ApplyProfile() call already set
        // activeProfileId_ -- nothing else to do.
        return;
    }
    DetectCurrentProfile();
}

const Profile* ProfileManager::FindProfileByIdLocked(const std::string& profileId) const {
    for (const Profile& profile : predefinedProfiles_) {
        if (profile.id == profileId) {
            return &profile;
        }
    }
    for (const Profile& profile : customProfiles_) {
        if (profile.id == profileId) {
            return &profile;
        }
    }
    return nullptr;
}

std::vector<Profile> ProfileManager::GetCustomProfiles() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return customProfiles_;
}

std::optional<std::string> ProfileManager::GetActiveProfileId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return activeProfileId_;
}

bool ProfileManager::ProfileExists(const std::string& profileId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return FindProfileByIdLocked(profileId) != nullptr;
}

void ProfileManager::SetActiveProfileIdLocked(std::optional<std::string> id) {
    activeProfileId_ = std::move(id);
}

std::optional<Profile> ProfileManager::GetProfileById(const std::string& profileId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Profile* profile = FindProfileByIdLocked(profileId);
    if (!profile) {
        return std::nullopt;
    }
    return *profile;
}

std::vector<AppliedVariableResult> ProfileManager::ApplyProfile(const std::string& profileId) {
    // Copy the profile's variables under the lock, then release it before
    // doing any Win32/WMI/COM I/O below -- ApplyProfile can be called from
    // either the UI thread (manual click) or the data-tick thread
    // (AutomationEngine::Tick()), and holding mutex_ across slow DDC/CI/WMI
    // calls would block the other thread's profile/rule CRUD for no reason.
    ProfileVariables vars;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Profile* profile = FindProfileByIdLocked(profileId);
        if (!profile) {
            return {};
        }
        vars = profile->vars;
    }

    std::vector<AppliedVariableResult> results;
    ComScope comScope;

    results.push_back({"powerPlan", PowerPlanControl::SetActiveScheme(vars.powerPlan)});
    results.push_back({"screenOffTimeout",
                        PowerTimeouts::SetScreenOffTimeouts(vars.screenOffTimeoutAcSec, vars.screenOffTimeoutDcSec)});
    results.push_back({"sleepTimeout",
                        PowerTimeouts::SetSleepTimeouts(vars.sleepTimeoutAcSec, vars.sleepTimeoutDcSec)});
    results.push_back(
        {"hibernateTimeout",
         PowerTimeouts::SetHibernateTimeouts(vars.hibernateTimeoutAcSec, vars.hibernateTimeoutDcSec)});
    results.push_back({"brightness", DisplayBrightness::SetBrightness(vars.brightnessPercent)});

    ApplyOverridable(results, "volume", vars.volumePercent, &Volume::SetVolume);

    // Setting the active id happens regardless of how many variables above
    // failed or came back Unsupported — a partially-applied profile is still
    // "the one you asked for", never blocked over one failing variable.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SetActiveProfileIdLocked(profileId);
    }
    return results;
}

void ProfileManager::DetectCurrentProfile() {
    // Snapshot the profile lists under the lock so the live-state reads
    // below (real I/O, can be slow) never run while holding mutex_.
    std::vector<Profile> predefinedSnapshot;
    std::vector<Profile> customSnapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        predefinedSnapshot = predefinedProfiles_;
        customSnapshot = customProfiles_;
    }

    ComScope comScope;

    std::optional<PowerPlan> currentPowerPlan = PowerPlanControl::GetActiveScheme();
    std::optional<int> currentScreenOffAc = PowerTimeouts::GetScreenOffTimeoutAc();
    std::optional<int> currentScreenOffDc = PowerTimeouts::GetScreenOffTimeoutDc();
    std::optional<int> currentSleepAc = PowerTimeouts::GetSleepTimeoutAc();
    std::optional<int> currentSleepDc = PowerTimeouts::GetSleepTimeoutDc();
    std::optional<int> currentHibernateAc = PowerTimeouts::GetHibernateTimeoutAc();
    std::optional<int> currentHibernateDc = PowerTimeouts::GetHibernateTimeoutDc();
    std::optional<int> currentBrightness = DisplayBrightness::GetBrightness();
    std::optional<int> currentVolume = Volume::GetVolume();

    auto matches = [&](const Profile& profile) {
        if (!currentPowerPlan.has_value() || *currentPowerPlan != profile.vars.powerPlan) {
            return false;
        }
        if (!currentScreenOffAc.has_value() || *currentScreenOffAc != profile.vars.screenOffTimeoutAcSec) {
            return false;
        }
        if (!currentScreenOffDc.has_value() || *currentScreenOffDc != profile.vars.screenOffTimeoutDcSec) {
            return false;
        }
        if (!currentSleepAc.has_value() || *currentSleepAc != profile.vars.sleepTimeoutAcSec) {
            return false;
        }
        if (!currentSleepDc.has_value() || *currentSleepDc != profile.vars.sleepTimeoutDcSec) {
            return false;
        }
        if (!currentHibernateAc.has_value() || *currentHibernateAc != profile.vars.hibernateTimeoutAcSec) {
            return false;
        }
        if (!currentHibernateDc.has_value() || *currentHibernateDc != profile.vars.hibernateTimeoutDcSec) {
            return false;
        }

        // Brightness compares with tolerance, and only if currently readable
        // at all (best-effort surface — unreadable is skipped, not a
        // mismatch, same as volume below).
        if (currentBrightness.has_value()) {
            int diff = *currentBrightness - profile.vars.brightnessPercent;
            if (diff < -2 || diff > 2) {
                return false;
            }
        }

        if (OverridableMismatches(profile.vars.volumePercent, currentVolume)) {
            return false;
        }

        return true;
    };

    std::optional<std::string> matchedId;
    for (const Profile& profile : predefinedSnapshot) {
        if (matches(profile)) {
            matchedId = profile.id;
            break;
        }
    }
    if (!matchedId.has_value()) {
        for (const Profile& profile : customSnapshot) {
            if (matches(profile)) {
                matchedId = profile.id;
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    SetActiveProfileIdLocked(matchedId);
}

std::string ProfileManager::GenerateCustomProfileId() {
    GUID guid;
    // Falls back to a fixed placeholder in the (essentially impossible) event
    // CoCreateGuid fails, rather than leaving customProfiles_ with a
    // duplicate/empty id.
    if (FAILED(CoCreateGuid(&guid))) {
        return "custom.fallback";
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "custom.%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
                  guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return std::string(buffer);
}

std::string ProfileManager::CreateProfile(const std::string& name, const std::string& icon,
                                           const ProfileVariables& vars) {
    Profile p;
    p.id = GenerateCustomProfileId();
    p.name = name;
    p.icon = icon;
    p.isPredefined = false;
    p.vars = vars;

    std::lock_guard<std::mutex> lock(mutex_);
    customProfiles_.push_back(p);
    ProfileStore::Save(customProfiles_);
    return p.id;
}

bool ProfileManager::UpdateProfile(const std::string& id, const std::string& name,
                                    const std::string& icon, const ProfileVariables& vars) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Profile& profile : customProfiles_) {
        if (profile.id != id) {
            continue;
        }
        if (profile.isPredefined) {
            return false;
        }
        profile.name = name;
        profile.icon = icon;
        profile.vars = vars;
        ProfileStore::Save(customProfiles_);
        return true;
    }
    return false;
}

bool ProfileManager::DeleteProfile(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(customProfiles_.begin(), customProfiles_.end(),
                            [&](const Profile& profile) { return profile.id == id; });
    if (it == customProfiles_.end() || it->isPredefined) {
        return false;
    }

    customProfiles_.erase(it);
    ProfileStore::Save(customProfiles_);

    if (activeProfileId_.has_value() && *activeProfileId_ == id) {
        SetActiveProfileIdLocked(std::nullopt);
    }
    return true;
}

std::vector<Profile> ProfileManager::BuildPredefinedProfiles() {
    std::vector<Profile> list;

    // Ids are fixed strings — never change these once shipped, custom
    // profiles/automation rules may reference them by id.
    {
        Profile p;
        p.id = "predef.rendimiento";
        p.name = "Rendimiento";
        p.icon = "R";
        p.isPredefined = true;
        p.vars.powerPlan = PowerPlan::UltimatePerformance;
        p.vars.screenOffTimeoutAcSec = 1800;
        p.vars.screenOffTimeoutDcSec = 900;
        p.vars.sleepTimeoutAcSec = 3600;
        p.vars.sleepTimeoutDcSec = 1800;
        p.vars.hibernateTimeoutAcSec = 7200;
        p.vars.hibernateTimeoutDcSec = 3600;
        p.vars.brightnessPercent = 100;
        list.push_back(p);
    }
    {
        Profile p;
        p.id = "predef.equilibrado";
        p.name = "Equilibrado";
        p.icon = "E";
        p.isPredefined = true;
        p.vars.powerPlan = PowerPlan::Balanced;
        p.vars.screenOffTimeoutAcSec = 600;
        p.vars.screenOffTimeoutDcSec = 300;
        p.vars.sleepTimeoutAcSec = 1800;
        p.vars.sleepTimeoutDcSec = 900;
        p.vars.hibernateTimeoutAcSec = 3600;
        p.vars.hibernateTimeoutDcSec = 1800;
        p.vars.brightnessPercent = 80;
        list.push_back(p);
    }
    {
        Profile p;
        p.id = "predef.ahorro_bateria";
        p.name = "Ahorro de bateria";
        p.icon = "B";
        p.isPredefined = true;
        p.vars.powerPlan = PowerPlan::Saver;
        p.vars.screenOffTimeoutAcSec = 300;
        p.vars.screenOffTimeoutDcSec = 120;
        p.vars.sleepTimeoutAcSec = 900;
        p.vars.sleepTimeoutDcSec = 300;
        p.vars.hibernateTimeoutAcSec = 1800;
        p.vars.hibernateTimeoutDcSec = 600;
        p.vars.brightnessPercent = 60;
        list.push_back(p);
    }
    {
        Profile p;
        p.id = "predef.silencio_noche";
        p.name = "Silencio/Noche";
        p.icon = "N";
        p.isPredefined = true;
        p.vars.powerPlan = PowerPlan::Balanced;
        p.vars.screenOffTimeoutAcSec = 180;
        p.vars.screenOffTimeoutDcSec = 120;
        p.vars.sleepTimeoutAcSec = 600;
        p.vars.sleepTimeoutDcSec = 300;
        p.vars.hibernateTimeoutAcSec = 1200;
        p.vars.hibernateTimeoutDcSec = 600;
        p.vars.brightnessPercent = 60;
        p.vars.volumePercent.apply = true;
        p.vars.volumePercent.value = 70;
        list.push_back(p);
    }
    {
        Profile p;
        p.id = "predef.presentacion_multimedia";
        p.name = "Presentacion/Multimedia";
        p.icon = "P";
        p.isPredefined = true;
        p.vars.powerPlan = PowerPlan::HighPerformance;
        p.vars.screenOffTimeoutAcSec = 0; // never
        p.vars.screenOffTimeoutDcSec = 1800;
        p.vars.sleepTimeoutAcSec = 0; // never
        p.vars.sleepTimeoutDcSec = 3600;
        // No hibernation for this profile -- stays at the "never" default.
        p.vars.brightnessPercent = 100;
        list.push_back(p);
    }

    return list;
}

} // namespace profiles
