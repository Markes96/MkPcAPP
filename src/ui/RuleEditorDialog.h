#pragma once
#include "../profiles/AutomationEngine.h"
#include "../profiles/ProfileManager.h"
#include "../profiles/ProfileTypes.h"

namespace ui {

// Modal create dialog for automation rules, mirroring ProfileEditorDialog's
// shape/idiom. Editing an existing rule isn't needed yet -- PerfilesTab's
// checkbox already toggles enabled in place, and delete/reorder cover the
// rest; a rule whose details need changing is deleted and re-created. Render()
// must be called every frame regardless of whether the dialog is open, same
// contract as ProfileEditorDialog.
class RuleEditorDialog {
public:
    void OpenForCreate();

    void Render(profiles::AutomationEngine& automationEngine, profiles::ProfileManager& profileManager);

private:
    bool isOpen_ = false;
    bool openRequested_ = false;
    profiles::AutomationRule rule_;
    // Time-of-day fields are edited as separate hour/minute sliders (simpler
    // and clearer than one 0..1439 slider) and folded into
    // rule_.start/endMinuteOfDay right before saving.
    int startHour_ = 23;
    int startMinute_ = 0;
    int endHour_ = 7;
    int endMinute_ = 0;
    int targetProfileIndex_ = 0;
};

} // namespace ui
