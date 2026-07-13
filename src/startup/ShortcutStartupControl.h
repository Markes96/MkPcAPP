#pragma once
#include "StartupTypes.h"
#include <vector>

namespace startup::ShortcutStartupControl {

// Both Enumerate* functions resolve each shortcut's target via IShellLink,
// so they must be called with COM already initialized on the calling thread
// (StartupScanner::Scan() owns that pairing for a full rescan).
std::vector<StartupEntry> EnumerateUserStartupFolder();
std::vector<StartupEntry> EnumerateCommonStartupFolder();

// Moves the .lnk between its Startup folder and a "Disabled" subfolder next
// to it (created on demand) -- never deletes the file. Updates
// entry.shortcutFilePath/enabled on success so a caller doesn't have to wait
// for the next rescan to see the change reflected.
bool SetShortcutEnabled(StartupEntry& entry, bool enabled);

// Sends the .lnk to the Recycle Bin (never the shortcut's target exe --
// the shortcut itself is just an autostart pointer, this is the delete
// counterpart to SetShortcutEnabled). Requires COM already initialized on
// this thread. A missing file counts as success: the desired end state
// ("this doesn't autostart") already holds.
bool DeleteToRecycleBin(const StartupEntry& entry);

} // namespace startup::ShortcutStartupControl
