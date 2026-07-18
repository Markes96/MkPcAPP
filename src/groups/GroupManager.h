#pragma once
#include "GroupTypes.h"
#include <string>
#include <vector>

namespace groups {

// Owns the full list of user-defined launch groups, loaded once from
// GroupStore and kept in memory afterward -- every CRUD method persists
// immediately. Unlike profiles::ProfileManager, this needs no mutex:
// nothing on the background data-tick thread reads or writes group
// definitions (GroupProcessTracker::Tick only touches runtime process
// state, never this class) -- only ever touched from the render thread,
// via GroupsTab's button/dialog handlers.
class GroupManager {
public:
    void Init(); // loads groups_ from GroupStore::Load()

    const std::vector<LaunchGroup>& GetGroups() const { return groups_; }

    // Generates a fresh id ("group.<guid>"), appends, persists, returns
    // the new id.
    std::string CreateGroup(const std::string& name, const std::vector<LaunchEntry>& entries);

    // Updates an existing group in place and persists. Returns false
    // (no-op) if id isn't found.
    bool UpdateGroup(const std::string& id, const std::string& name, const std::vector<LaunchEntry>& entries);

    // Removes a group and persists. Returns false (no-op) if id isn't found.
    bool DeleteGroup(const std::string& id);

private:
    static std::string GenerateGroupId();
    std::vector<LaunchGroup> groups_;
};

} // namespace groups
