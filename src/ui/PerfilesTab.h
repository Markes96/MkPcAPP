#pragma once
#include "ITab.h"
#include "ProfileEditorDialog.h"
#include "RuleEditorDialog.h"
#include "../profiles/AutomationEngine.h"
#include "../profiles/ProfileManager.h"
#include "../profiles/ProfileTypes.h"
#include <windows.h>
#include <vector>

namespace ui {

// The "Perfiles" section: one-click switching between profiles that bundle
// several Windows settings (power plan, timeouts, brightness, and best-effort
// volume) at once, plus the automation rule list
// below it. Reads/writes only through profiles::ProfileManager and
// profiles::AutomationEngine — never touches Win32/WMI/COM directly.
class PerfilesTab : public ITab {
public:
    PerfilesTab(profiles::ProfileManager& profileManager, profiles::AutomationEngine& automationEngine, HWND hwnd)
        : profileManager_(profileManager), automationEngine_(automationEngine), hwnd_(hwnd) {}

    const char* GetTitle() const override { return "Perfiles"; }
    const char* GetIcon() const override { return "P"; }
    void OnRender(float deltaTimeSeconds) override;

private:
    void RenderTopBar();
    void RenderProfileCard(const profiles::Profile& profile);
    void RenderApplyFeedback();
    void RenderAutomationSection();
    std::string DescribeRule(const profiles::AutomationRule& rule) const;

    profiles::ProfileManager& profileManager_;
    profiles::AutomationEngine& automationEngine_;
    HWND hwnd_;
    std::vector<profiles::AppliedVariableResult> lastApplyResults_;
    ui::ProfileEditorDialog profileEditorDialog_;
    ui::RuleEditorDialog ruleEditorDialog_;
};

} // namespace ui
