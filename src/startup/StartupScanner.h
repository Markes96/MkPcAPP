#pragma once
#include "StartupTypes.h"
#include "SignatureVerifier.h"
#include "RegistryStartupControl.h"
#include <mutex>
#include <string>
#include <vector>

namespace startup {

struct ScanResult {
    std::vector<StartupEntry> entries;
};

// Orchestrator: enumerates all four sources (HKCU/HKLM Run, both Startup
// folders), applies the Microsoft-signature filter, and dispatches
// enable/disable/add/delete to the right per-source control module. Owns no
// D3D/ImGui state -- icon texture caching lives entirely in
// platform::IconTextureCache, owned by the UI layer instead.
//
// Thread-safe: Scan() is expected to run on StartupTab's OnTick (background
// data-tick thread) while SetEnabled/AddManualEntry/DeleteManualEntry are
// called from OnRender (main thread) in response to user clicks --
// signatureVerifier_'s cache and manuallyAddedValueNames_ are the only state
// shared between those calls, guarded by mutex_ the same way
// AutomationEngine guards rules_.
class StartupScanner {
public:
    ScanResult Scan();

    // Dispatches to RegistryStartupControl or ShortcutStartupControl based
    // on entry.source. Updates entry in place on success.
    bool SetEnabled(StartupEntry& entry, bool enabled);

    // Always creates a HKCU Run value (never HKLM), per the approved
    // "manual add always goes to the current user" design.
    RegistryStartupControl::AddResult AddManualEntry(const std::wstring& displayName,
                                                       const std::wstring& exePath);

    // Only valid for entries with deletable == true (created by
    // AddManualEntry this same session).
    bool DeleteManualEntry(const StartupEntry& entry);

private:
    std::mutex mutex_; // guards signatureVerifier_ and manuallyAddedValueNames_ only
    SignatureVerifier signatureVerifier_;
    // Value names added via AddManualEntry() this session -- the only
    // entries that get deletable=true on subsequent Scan() calls (see
    // StartupScanner.cpp for why this doesn't survive an app restart).
    std::vector<std::wstring> manuallyAddedValueNames_;
};

} // namespace startup
