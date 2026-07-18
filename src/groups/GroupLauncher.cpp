#include "GroupLauncher.h"
#include "ProcessLifecycle.h"
#include "../platform/StringConvert.h"

namespace groups {

std::vector<EntryOpenOutcome> GroupLauncher::OpenGroup(const std::string& groupId,
                                                        const std::vector<LaunchEntry>& entries) {
    std::vector<EntryOpenOutcome> outcomes;
    outcomes.reserve(entries.size());

    for (const LaunchEntry& entry : entries) {
        EntryOpenOutcome outcome;

        if (entry.resolvedExePath.empty()) {
            outcome.status = EntryOpenStatus::LaunchFailed;
            outcomes.push_back(outcome);
            continue;
        }

        std::wstring widePath = platform::Utf8ToWide(entry.resolvedExePath);

        // Already claimed by another currently-open group -- join as
        // co-owner, don't relaunch. See the design spec's corrected
        // "Flujo Abrir grupo" for why this check comes before the
        // system-wide IsProcessRunning check below.
        if (tracker_.IsPathCurrentlyOwned(widePath)) {
            tracker_.JoinExistingOwnership(groupId, widePath);
            outcome.status = EntryOpenStatus::JoinedExisting;
            outcomes.push_back(outcome);
            continue;
        }

        if (tracker_.ReclaimPending(groupId, widePath)) {
            // Reopened within the grace period of this same group's own
            // "Cerrar grupo" -- the app never actually exited, so cancel
            // its pending force-close and reclaim it instead of treating
            // it as externally owned (which would let the stale
            // force-close kill it a few ticks later).
            outcome.status = EntryOpenStatus::Launched;
            outcomes.push_back(outcome);
            continue;
        }

        // Running for a reason the tracker doesn't know about (normally:
        // the user already had it open) -- leave it alone entirely.
        if (IsProcessRunning(widePath)) {
            outcome.status = EntryOpenStatus::ExternallyOwned;
            outcomes.push_back(outcome);
            continue;
        }

        LaunchResult launch = LaunchProcess(widePath, platform::Utf8ToWide(entry.args));
        if (!launch.ok) {
            outcome.status = EntryOpenStatus::LaunchFailed;
            outcomes.push_back(outcome);
            continue;
        }

        tracker_.RegisterOwnedLaunch(groupId, widePath, launch.pid, launch.processHandle);
        outcome.status = EntryOpenStatus::Launched;
        outcomes.push_back(outcome);
    }

    return outcomes;
}

void GroupLauncher::CloseGroup(const std::string& groupId, const std::vector<LaunchEntry>& entries,
                                uint64_t nowTickCount) {
    for (const LaunchEntry& entry : entries) {
        if (entry.resolvedExePath.empty()) {
            continue;
        }
        tracker_.ReleaseOwnership(groupId, platform::Utf8ToWide(entry.resolvedExePath), nowTickCount);
    }
}

} // namespace groups
