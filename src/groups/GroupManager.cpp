#include "GroupManager.h"
#include "GroupStore.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>

namespace groups {

void GroupManager::Init() {
    groups_ = GroupStore::Load();
}

std::string GroupManager::GenerateGroupId() {
    GUID guid;
    // Falls back to a fixed placeholder in the (essentially impossible)
    // event CoCreateGuid fails, rather than leaving groups_ with a
    // duplicate/empty id -- same idiom as
    // profiles::ProfileManager::GenerateCustomProfileId.
    if (FAILED(CoCreateGuid(&guid))) {
        return "group.fallback";
    }

    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "group.%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  static_cast<unsigned long>(guid.Data1), guid.Data2, guid.Data3, guid.Data4[0],
                  guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return std::string(buffer);
}

std::string GroupManager::CreateGroup(const std::string& name, const std::vector<LaunchEntry>& entries) {
    LaunchGroup group;
    group.id = GenerateGroupId();
    group.name = name;
    group.entries = entries;

    groups_.push_back(group);
    GroupStore::Save(groups_);
    return group.id;
}

bool GroupManager::UpdateGroup(const std::string& id, const std::string& name,
                                const std::vector<LaunchEntry>& entries) {
    for (LaunchGroup& group : groups_) {
        if (group.id == id) {
            group.name = name;
            group.entries = entries;
            GroupStore::Save(groups_);
            return true;
        }
    }
    return false;
}

bool GroupManager::DeleteGroup(const std::string& id) {
    auto it = std::find_if(groups_.begin(), groups_.end(),
                            [&id](const LaunchGroup& g) { return g.id == id; });
    if (it == groups_.end()) {
        return false;
    }
    groups_.erase(it);
    GroupStore::Save(groups_);
    return true;
}

} // namespace groups
