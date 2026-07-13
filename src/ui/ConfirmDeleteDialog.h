#pragma once
#include "../startup/StartupTypes.h"
#include "../startup/StartupScanner.h"
#include <string>

namespace ui {

// Confirmation modal shown before deleting ANY startup entry (registry
// value or shortcut) -- deleting is irreversible from within the app for
// registry values (shortcut deletes land in the Recycle Bin, but still
// worth the same confirmation for consistency). Same idiom as
// AddStartupEntryDialog: OpenForEntry() captures its own copy of the
// entry (not a live reference -- the underlying list can mutate between
// opening this dialog and the user confirming), Render() is called
// unconditionally every frame.
class ConfirmDeleteDialog {
public:
    void OpenForEntry(const startup::StartupEntry& entry);
    void Render(startup::StartupScanner& scanner);

    // Returns true exactly once, right after a successful delete, then
    // resets -- lets StartupTab trigger an immediate rescan.
    bool ConsumeJustDeleted();

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    bool justDeleted_ = false;
    startup::StartupEntry pendingEntry_;
    std::string errorMessage_;
};

} // namespace ui
