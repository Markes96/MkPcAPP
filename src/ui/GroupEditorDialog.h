#pragma once
#include "../groups/GroupManager.h"
#include "../groups/GroupTypes.h"
#include <optional>
#include <string>
#include <vector>

namespace ui {

// Modal create/edit dialog for launch groups, driven by GroupsTab. Same
// idiom as ProfileEditorDialog/AddStartupEntryDialog: OpenForCreate/
// OpenForEdit call ImGui::OpenPopup, Render() must run every frame
// regardless of whether the dialog is open. Unlike AddStartupEntryDialog's
// file picker (filtered to *.exe only), this one accepts any file --
// launch groups cover cases like Minecraft that run through a Java
// launcher, not a bare .exe.
class GroupEditorDialog {
public:
    void OpenForCreate();
    void OpenForEdit(const groups::LaunchGroup& group);

    void Render(groups::GroupManager& manager);

    // Returns true exactly once, right after a successful save, then
    // resets -- lets GroupsTab refresh its view immediately.
    bool ConsumeJustSaved();

private:
    // Returns true if this row's "Quitar" was clicked -- caller removes
    // it after the loop finishes rather than erasing entries_ mid-iteration.
    bool RenderEntryRow(size_t index);

    bool isOpen_ = false;
    bool openRequested_ = false;
    bool isEditMode_ = false;
    bool justSaved_ = false;
    std::string editingGroupId_;
    char nameBuffer_[128] = "";
    std::vector<groups::LaunchEntry> entries_;
};

} // namespace ui
