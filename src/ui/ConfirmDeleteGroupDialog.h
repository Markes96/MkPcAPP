#pragma once
#include "../groups/GroupLauncher.h"
#include "../groups/GroupManager.h"
#include "../groups/GroupTypes.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

namespace ui {

// Confirmation modal shown before deleting a launch group. Same idiom as
// startup::ConfirmDeleteDialog (kept as a separate small class rather
// than shared/templated, since that one is coupled to
// startup::StartupEntry/StartupScanner): OpenForGroup() captures its own
// copy of the group (not a live reference -- the underlying list can
// mutate between opening this dialog and the user confirming), Render()
// is called unconditionally every frame.
//
// If the group being deleted is currently open (per openGroupIds,
// GroupsTab's own tracking), confirming the delete closes it first (same
// as pressing "Cerrar grupo") so no process is left orphaned in
// GroupProcessTracker -- see the design spec's UI section.
class ConfirmDeleteGroupDialog {
public:
    void OpenForGroup(const groups::LaunchGroup& group);
    void Render(groups::GroupManager& manager, groups::GroupLauncher& launcher,
                const std::unordered_set<std::string>& openGroupIds, uint64_t tickCount);

    // Returns the id of the group just deleted, exactly once, then
    // resets to nullopt -- lets GroupsTab drop it from its own "which
    // groups are open" tracking and refresh its view immediately.
    std::optional<std::string> ConsumeJustDeleted();

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    std::optional<std::string> justDeletedId_;
    groups::LaunchGroup pendingGroup_;
};

} // namespace ui
