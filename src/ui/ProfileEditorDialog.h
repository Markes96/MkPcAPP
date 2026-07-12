#pragma once
#include "../profiles/ProfileManager.h"
#include "../profiles/ProfileTypes.h"
#include <string>

namespace ui {

// Modal create/edit dialog for custom profiles, driven by PerfilesTab. Not an
// ITab itself -- just a small self-contained popup helper owned by the tab
// that shows it. Standard ImGui modal idiom: OpenForCreate/OpenForEdit call
// ImGui::OpenPopup, and Render() must be called every frame regardless of
// whether the dialog is open so the popup gets a chance to process.
class ProfileEditorDialog {
public:
    // Seeds the new profile's defaults from the real "predef.equilibrado"
    // profile via profileManager, instead of a second hand-kept copy of its
    // values.
    void OpenForCreate(const profiles::ProfileManager& profileManager);
    void OpenForEdit(const profiles::Profile& profile);

    void Render(profiles::ProfileManager& profileManager);

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    bool isEditMode_ = false;
    std::string editingProfileId_;
    char nameBuffer_[64] = "";
    char iconBuffer_[8] = "";
    profiles::ProfileVariables vars_;
};

} // namespace ui
