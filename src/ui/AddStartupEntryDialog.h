#pragma once
#include "../startup/StartupScanner.h"
#include <string>

namespace ui {

// Modal "add" dialog for manually registering a new startup entry, driven
// by StartupTab. Same idiom as ProfileEditorDialog: OpenForCreate() calls
// ImGui::OpenPopup, and Render() must be called every frame regardless of
// whether the dialog is open. Always creates a HKCU Run value (never HKLM),
// per the approved design.
class AddStartupEntryDialog {
public:
    void OpenForCreate();
    void Render(startup::StartupScanner& scanner);

    // Returns true exactly once, right after a successful save, then resets
    // -- lets StartupTab trigger an immediate rescan so the new entry shows
    // up right away instead of waiting for the next periodic tick.
    bool ConsumeJustSaved();

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    bool justSaved_ = false;
    char nameBuffer_[128] = "";
    std::wstring chosenExePath_;
    std::string errorMessage_;
};

} // namespace ui
