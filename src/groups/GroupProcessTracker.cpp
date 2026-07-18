#include "GroupProcessTracker.h"
#include "ProcessLifecycle.h"

namespace groups {

namespace {
// ~5 ticks at the app's 1 Hz data-tick rate -- "esperar unos segundos"
// per the design spec's "Flujo Cerrar grupo".
constexpr uint64_t kForceCloseDelayTicks = 5;
} // namespace

GroupProcessTracker::~GroupProcessTracker() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [path, owned] : ownedByPath_) {
        if (owned.processHandle) {
            CloseHandle(owned.processHandle);
        }
    }
    for (auto& pending : pendingForceClose_) {
        if (pending.processHandle) {
            CloseHandle(pending.processHandle);
        }
    }
}

bool GroupProcessTracker::IsPathCurrentlyOwned(const std::wstring& resolvedExePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ownedByPath_.find(resolvedExePath) != ownedByPath_.end();
}

void GroupProcessTracker::JoinExistingOwnership(const std::string& groupId,
                                                 const std::wstring& resolvedExePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownedByPath_.find(resolvedExePath);
    if (it == ownedByPath_.end()) {
        return; // caller bug -- should have checked IsPathCurrentlyOwned first
    }
    it->second.ownerGroupIds.insert(groupId);
}

void GroupProcessTracker::RegisterOwnedLaunch(const std::string& groupId, const std::wstring& resolvedExePath,
                                               DWORD pid, HANDLE processHandle) {
    std::lock_guard<std::mutex> lock(mutex_);
    OwnedProcess& owned = ownedByPath_[resolvedExePath];
    owned.pid = pid;
    owned.processHandle = processHandle;
    owned.ownerGroupIds.insert(groupId);
}

void GroupProcessTracker::ReleaseOwnership(const std::string& groupId, const std::wstring& resolvedExePath,
                                            uint64_t nowTickCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ownedByPath_.find(resolvedExePath);
    if (it == ownedByPath_.end()) {
        return; // never tracked by anyone (external, or the launch had failed)
    }

    it->second.ownerGroupIds.erase(groupId);
    if (!it->second.ownerGroupIds.empty()) {
        return; // another open group still owns it -- leave it running
    }

    DWORD pid = it->second.pid;
    HANDLE handle = it->second.processHandle;
    ownedByPath_.erase(it);

    RequestGracefulClose(pid); // non-blocking, sends WM_CLOSE now

    PendingForceClose pending;
    pending.resolvedExePath = resolvedExePath;
    pending.processHandle = handle;
    pending.deadlineTick = nowTickCount + kForceCloseDelayTicks;
    pending.pid = pid;
    pendingForceClose_.push_back(std::move(pending));
}

bool GroupProcessTracker::ReclaimPending(const std::string& groupId, const std::wstring& resolvedExePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pendingForceClose_.begin(); it != pendingForceClose_.end(); ++it) {
        if (it->resolvedExePath == resolvedExePath) {
            OwnedProcess owned;
            owned.pid = it->pid;
            owned.processHandle = it->processHandle;
            owned.ownerGroupIds.insert(groupId);
            ownedByPath_[resolvedExePath] = std::move(owned);
            pendingForceClose_.erase(it);
            return true;
        }
    }
    return false;
}

void GroupProcessTracker::Tick(uint64_t tickCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pendingForceClose_.begin(); it != pendingForceClose_.end();) {
        if (!IsProcessAlive(it->processHandle)) {
            CloseHandle(it->processHandle);
            it = pendingForceClose_.erase(it);
            continue;
        }
        if (tickCount >= it->deadlineTick) {
            if (!ForceTerminate(it->processHandle)) {
                closeFailures_.push_back(it->resolvedExePath);
            }
            CloseHandle(it->processHandle);
            it = pendingForceClose_.erase(it);
            continue;
        }
        ++it;
    }
}

std::vector<std::wstring> GroupProcessTracker::ConsumeCloseFailures() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> result = std::move(closeFailures_);
    closeFailures_.clear();
    return result;
}

} // namespace groups
