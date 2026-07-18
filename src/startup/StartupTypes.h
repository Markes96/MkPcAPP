#pragma once
#include <string>

namespace startup {

enum class StartupSource {
    RegistryHkcuRun,
    RegistryHklmRun,
    StartupFolderUser,
    StartupFolderCommon,
};

struct StartupEntry {
    // Stable across rescans: hive+valueName for registry sources, or
    // source+filename (without extension) for shortcut sources -- the
    // shortcut's actual file path changes when it moves in/out of the
    // "Disabled" subfolder, so the path itself can't be the key.
    std::string id;
    StartupSource source = StartupSource::RegistryHkcuRun;
    std::string displayName;
    std::string resolvedExePath; // empty if unresolved (see targetMissing)

    // Only meaningful for RegistryHkcuRun/RegistryHklmRun sources.
    std::wstring registryValueName;
    // Only meaningful for RegistryHklmRun: true if this value came from the
    // WOW6432Node mirror rather than the regular key -- needed to pick the
    // matching StartupApproved subpath when toggling enable/disable.
    bool isWow6432 = false;

    // Only meaningful for StartupFolderUser/StartupFolderCommon sources; the
    // shortcut's current full path (base folder or its "Disabled" subfolder).
    std::wstring shortcutFilePath;

    bool enabled = true;
    // The resolved exe (or shortcut target) didn't exist on disk at scan
    // time -- still shown (never hidden), signature check and icon
    // extraction are skipped for it.
    bool targetMissing = false;
    // True if resolvedExePath sits under %SystemRoot%\System32 or SysWOW64
    // and it wasn't excluded by the Microsoft-signature filter (i.e. it's
    // not actually Microsoft's own binary). Legitimate non-Microsoft
    // software essentially never installs there -- Windows Resource
    // Protection blocks unelevated writes to it -- so something claiming
    // that location without a Microsoft signature is a red flag worth
    // surfacing, not a reason to hide the entry outright (unlike
    // targetMissing/Microsoft-signed, path alone isn't proof of anything).
    bool isUnderSystem32 = false;
};

} // namespace startup
