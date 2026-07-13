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
// data-tick thread) while SetEnabled/AddManualEntry/DeleteEntry/
// GetSignatureInfo are called from OnRender (main thread) in response to
// user clicks -- signatureVerifier_'s cache is the only state shared
// between those calls, guarded by mutex_ the same way AutomationEngine
// guards rules_.
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

    // Valid for ANY entry, not just ones added through this app --
    // Registry entries have their Run value deleted (never the .exe);
    // Startup-folder shortcuts are sent to the Recycle Bin (never the
    // target .exe). A "not found" outcome (already deleted by something
    // else) counts as success: the desired end state already holds.
    bool DeleteEntry(const StartupEntry& entry);

    // Used by the "info" popup. Thread-safe wrapper around
    // SignatureVerifier::GetSignatureInfo -- shares the same cache Scan()
    // populates via IsMicrosoftSigned.
    SignatureInfo GetSignatureInfo(const std::wstring& exePath);

private:
    std::mutex mutex_; // guards signatureVerifier_ only
    SignatureVerifier signatureVerifier_;
};

} // namespace startup
