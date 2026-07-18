#pragma once
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace groups {

// Cross-group reference counting for processes this app itself launched
// via GroupLauncher, keyed by resolved executable path (not PID, since
// relaunching the same app produces a different PID). Two groups that
// both list the same app (e.g. Discord in both "League of Legends" and
// "Minecraft") share one entry here -- the process is only actually
// closed once no currently-open group still owns it. A process the user
// already had running before any group's "Abrir grupo" was clicked is
// never registered here at all, so ReleaseOwnership never touches it --
// see the design spec's corrected "Flujo Abrir grupo".
//
// Also owns the "graceful close, then force after a timeout" bookkeeping:
// ReleaseOwnership requests a graceful WM_CLOSE immediately (via
// groups::RequestGracefulClose) and schedules a force-terminate check a
// few ticks later; Tick() (called from GroupsTab::OnTick, the background
// 1 Hz thread) carries out that force-terminate and records any
// failures, drained via ConsumeCloseFailures(). Every field here is
// in-memory only -- see the design spec's "estado abierto/cerrado...
// no persiste" limitation: a MkPCApp.exe restart loses this bookkeeping
// (the launched apps themselves keep running -- see the destructor).
//
// Every public method locks mutex_ -- safe to call from both the render
// thread (RegisterOwnedLaunch/JoinExistingOwnership/ReleaseOwnership/
// IsPathCurrentlyOwned, from GroupsTab's button handlers) and the
// background data-tick thread (Tick/ConsumeCloseFailures).
class GroupProcessTracker {
public:
    // Releases (but does not terminate) every process handle still
    // tracked at shutdown -- MkPCApp.exe exiting must never kill apps a
    // group launched (a game, Discord, ...); only closing the group
    // explicitly does that.
    ~GroupProcessTracker();

    bool IsPathCurrentlyOwned(const std::wstring& resolvedExePath) const;

    // Joins groupId onto the existing owner set for resolvedExePath
    // without launching anything -- used when OpenGroup finds the path
    // already tracked (owned by another currently-open group). No-op if
    // resolvedExePath isn't tracked at all (caller bug -- must check
    // IsPathCurrentlyOwned first).
    void JoinExistingOwnership(const std::string& groupId, const std::wstring& resolvedExePath);

    // Registers groupId as the (first) owner of a process this app just
    // launched. Call only right after LaunchProcess succeeded for a path
    // that wasn't already tracked.
    void RegisterOwnedLaunch(const std::string& groupId, const std::wstring& resolvedExePath, DWORD pid,
                              HANDLE processHandle);

    // Removes groupId from the owners of resolvedExePath. No-op if
    // groupId never owned it (e.g. it was external, or the launch had
    // failed). If this was the last owner, requests a graceful close now
    // and schedules a force-terminate check for
    // `nowTickCount + kForceCloseDelayTicks`.
    void ReleaseOwnership(const std::string& groupId, const std::wstring& resolvedExePath, uint64_t nowTickCount);

    // If `resolvedExePath` has a pending force-close scheduled (this app
    // was released by its last owning group but hasn't been confirmed
    // exited or force-terminated yet), cancels that pending close and
    // re-registers the same already-running process (same PID/handle, no
    // relaunch) as owned by `groupId` instead. Returns true if a pending
    // entry was found and reclaimed, false otherwise (nothing pending for
    // this path). Exists so that reopening a group within the grace
    // period of its own "Cerrar grupo" doesn't get the app force-killed
    // out from under the user -- see GroupLauncher::OpenGroup, which
    // calls this between the IsPathCurrentlyOwned and IsProcessRunning
    // checks.
    bool ReclaimPending(const std::string& groupId, const std::wstring& resolvedExePath);

    // Called once per tick (~1 Hz) from the background data-tick thread.
    // Force-terminates any pending close whose deadline has passed and
    // the process is still alive.
    void Tick(uint64_t tickCount);

    // Drains the list of paths that failed to force-close since the last
    // call -- GroupsTab surfaces these as a transient error message.
    std::vector<std::wstring> ConsumeCloseFailures();

private:
    struct OwnedProcess {
        DWORD pid = 0;
        HANDLE processHandle = nullptr;
        std::set<std::string> ownerGroupIds;
    };
    struct PendingForceClose {
        std::wstring resolvedExePath;
        HANDLE processHandle = nullptr;
        uint64_t deadlineTick = 0;
        DWORD pid = 0;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::wstring, OwnedProcess> ownedByPath_;
    std::vector<PendingForceClose> pendingForceClose_;
    std::vector<std::wstring> closeFailures_;
};

} // namespace groups
