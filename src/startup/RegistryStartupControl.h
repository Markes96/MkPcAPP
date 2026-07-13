#pragma once
#include "StartupTypes.h"
#include <vector>
#include <windows.h>

namespace startup::RegistryStartupControl {

// Enumerates HKEY_CURRENT_USER\...\CurrentVersion\Run. Each entry's
// StartupSource is RegistryHkcuRun; enabled/disabled is resolved from the
// sibling StartupApproved\Run key. Never throws -- an unreadable/missing key
// just yields an empty vector.
std::vector<StartupEntry> EnumerateHkcuRun();

// Enumerates HKEY_LOCAL_MACHINE\...\CurrentVersion\Run, plus its
// WOW6432Node mirror (32-bit entries on 64-bit Windows) -- both reported as
// StartupSource::RegistryHklmRun, since they're the same logical "all users"
// facility from the user's point of view.
std::vector<StartupEntry> EnumerateHklmRun();

// Flips only the enable/disable byte in StartupApproved\Run for this value,
// preserving any other bytes Explorer may have written (e.g. its
// last-changed timestamp) if a blob already exists. Never touches the Run
// value itself -- disabling never deletes the app's own autostart command.
// Returns false on any registry failure (key inaccessible even though this
// app runs elevated, etc.).
bool SetApprovedEnabled(HKEY hive, const std::wstring& valueName, bool isWow6432, bool enabled);

enum class AddResult { Ok, DuplicateName, Failed };

// Always writes to HKEY_CURRENT_USER\...\CurrentVersion\Run. Fails with
// DuplicateName rather than silently overwriting if a value with this name
// already exists.
AddResult AddUserRunEntry(const std::wstring& displayName, const std::wstring& quotedExePath);

// Deletes a Run value in the given hive (HKCU or HKLM), valid for ANY
// entry now (not just ones this app added) -- never touches the .exe the
// value points at, only the autostart registration itself. Best-effort
// also removes the matching StartupApproved value so no orphaned state
// blob lingers; that part's failure is ignored. A value that's already
// gone (ERROR_FILE_NOT_FOUND) counts as success: the desired end state
// ("this doesn't autostart") already holds.
bool DeleteRunEntry(HKEY hive, const std::wstring& valueName, bool isWow6432);

} // namespace startup::RegistryStartupControl
