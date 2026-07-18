#pragma once
#include "GroupProcessTracker.h"
#include "GroupTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace groups {

enum class EntryOpenStatus {
    Launched,          // this app started it, now sole owner
    JoinedExisting,     // already running, owned by another currently-open group -- joined as co-owner
    ExternallyOwned,    // already running for a reason the tracker doesn't know (e.g. user opened it) -- untouched
    LaunchFailed,       // CreateProcess failed, or the entry had no resolved exe path
};

struct EntryOpenOutcome {
    EntryOpenStatus status = EntryOpenStatus::LaunchFailed;
};

// Orchestrates opening/closing one LaunchGroup's entries, delegating
// cross-group reference counting to a GroupProcessTracker shared by
// every group in the app (owned by GroupsTab, constructed once, passed
// here by reference). Every method here is only ever called from the
// render thread (GroupsTab's button handlers) -- see
// GroupProcessTracker's own threading note for why Tick() is the one
// method called from the background thread instead.
class GroupLauncher {
public:
    explicit GroupLauncher(GroupProcessTracker& tracker) : tracker_(tracker) {}

    // One outcome per entry, same order as `entries`, so the caller can
    // show a per-entry error/note without aborting the rest of the group.
    std::vector<EntryOpenOutcome> OpenGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries);

    // Releases this group's ownership of every entry (a no-op per entry
    // if this group never owned it -- see GroupProcessTracker::
    // ReleaseOwnership). Actual process closing/force-close bookkeeping
    // happens inside tracker_; see GroupProcessTracker::Tick.
    void CloseGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries, uint64_t nowTickCount);

private:
    GroupProcessTracker& tracker_;
};

} // namespace groups
