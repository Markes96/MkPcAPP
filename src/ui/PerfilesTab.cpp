#include "PerfilesTab.h"
#include "Theme.h"
#include "CardWidgets.h"
#include "../profiles/SystemControl/ScreenControl.h"
#include <imgui.h>
#include <cstdio>
#include <optional>
#include <string>

namespace ui {

namespace {

constexpr float kCardHeight = 110.0f;
constexpr float kCardPadding = 10.0f;

// Same as CenteredText, but wraps instead of overflowing when the text is
// too wide to fit on one line -- used for profile names, which are short
// enough to center in the common case but not guaranteed to be. A name with
// a "/" (e.g. "Presentacion/Multimedia") is split into two centered lines at
// that character instead of relying on Dear ImGui's word-wrap, which only
// breaks at whitespace and would otherwise hard-break mid-word wherever it
// runs out of room, left-aligned and in an arbitrary spot.
void CenteredProfileName(const std::string& name) {
    size_t slashPos = name.find('/');
    if (slashPos != std::string::npos) {
        CenteredText(name.substr(0, slashPos).c_str());
        CenteredText(name.substr(slashPos + 1).c_str());
        return;
    }

    float availWidth = ImGui::GetContentRegionAvail().x;
    float textWidth = ImGui::CalcTextSize(name.c_str()).x;
    if (textWidth < availWidth) {
        CenteredText(name.c_str());
    } else {
        ImGui::TextWrapped("%s", name.c_str());
    }
}

const char* VariableDisplayName(const std::string& variableName) {
    if (variableName == "powerPlan") return "Plan de energia";
    if (variableName == "screenOffTimeout") return "Apagado de pantalla";
    if (variableName == "sleepTimeout") return "Suspension";
    if (variableName == "hibernateTimeout") return "Hibernacion";
    if (variableName == "brightness") return "Brillo";
    if (variableName == "volume") return "Volumen";
    return variableName.c_str();
}

// "Nunca" mirrors the "0 = nunca" convention already used throughout the
// profile editor for these timeout fields.
std::string MinutesOrNever(int seconds) {
    if (seconds <= 0) {
        return "Nunca";
    }
    return std::to_string(seconds / 60) + " min";
}

// Hover summary for a profile card -- lets the user see what a profile
// actually does without opening its editor (predefined profiles have no
// editor at all, being locked). Built fresh per hovered frame rather than
// cached, since it's cheap plain-data formatting.
std::string BuildProfileTooltip(const profiles::ProfileVariables& vars) {
    static const char* kPowerPlanNames[] = {"Ahorro", "Equilibrado", "Alto rendimiento",
                                             "Maximo rendimiento"};

    std::string text;
    text += "Plan de energia: ";
    text += kPowerPlanNames[static_cast<int>(vars.powerPlan)];
    text += "\nPantalla (corriente/bateria): " + MinutesOrNever(vars.screenOffTimeoutAcSec) + " / " +
            MinutesOrNever(vars.screenOffTimeoutDcSec);
    text += "\nSuspension (corriente/bateria): " + MinutesOrNever(vars.sleepTimeoutAcSec) + " / " +
            MinutesOrNever(vars.sleepTimeoutDcSec);
    text += "\nHibernacion (corriente/bateria): " + MinutesOrNever(vars.hibernateTimeoutAcSec) + " / " +
            MinutesOrNever(vars.hibernateTimeoutDcSec);
    text += "\nBrillo: " + std::to_string(vars.brightnessPercent) + "%";
    if (vars.volumePercent.apply) {
        text += "\nVolumen: " + std::to_string(vars.volumePercent.value) + "%";
    }
    return text;
}

const char* ApplyResultLabel(profiles::ApplyResult result) {
    switch (result) {
        case profiles::ApplyResult::Unsupported: return "no disponible";
        case profiles::ApplyResult::Failed: return "fallo al aplicar";
        case profiles::ApplyResult::Ok: return "ok";
    }
    return "";
}

} // namespace

void PerfilesTab::OnRender(float /*deltaTimeSeconds*/) {
    RenderTopBar();
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    if (RenderSectionHeaderAddButton("PERFILES")) {
        profileEditorDialog_.OpenForCreate(profileManager_);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    for (const profiles::Profile& profile : profileManager_.GetPredefinedProfiles()) {
        RenderProfileCard(profile);
    }
    // Iterate a snapshot copy rather than profileManager_.GetCustomProfiles()
    // directly: RenderProfileCard's "Borrar" button can call
    // DeleteProfile(), which erases from the live vector -- mutating the
    // container mid-range-for would invalidate this loop's iterator.
    std::vector<profiles::Profile> customProfilesSnapshot = profileManager_.GetCustomProfiles();
    for (const profiles::Profile& profile : customProfilesSnapshot) {
        RenderProfileCard(profile);
    }
    ImGui::NewLine();

    RenderApplyFeedback();

    RenderAutomationSection();

    profileEditorDialog_.Render(profileManager_);
    ruleEditorDialog_.Render(automationEngine_, profileManager_);
}

void PerfilesTab::RenderTopBar() {
    // Full available width captured before drawing anything, so the button
    // can be pinned to the right edge via SameLine(x) regardless of how long
    // the active-profile label ends up being.
    float fullWidth = ImGui::GetContentRegionAvail().x;

    std::optional<std::string> activeId = profileManager_.GetActiveProfileId();
    std::string activeLabel = "Sin perfil activo (config. personalizada detectada)";

    if (activeId.has_value()) {
        bool found = false;
        for (const profiles::Profile& profile : profileManager_.GetPredefinedProfiles()) {
            if (profile.id == *activeId) {
                activeLabel = profile.name;
                found = true;
                break;
            }
        }
        if (!found) {
            for (const profiles::Profile& profile : profileManager_.GetCustomProfiles()) {
                if (profile.id == *activeId) {
                    activeLabel = profile.name;
                    break;
                }
            }
        }
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Perfil activo: %s", activeLabel.c_str());

    const char* buttonLabel = "Apagar pantalla";
    float buttonWidth = ImGui::CalcTextSize(buttonLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(fullWidth - buttonWidth);
    if (ImGui::Button(buttonLabel)) {
        profiles::ScreenControl::TurnOffScreenNow(hwnd_);
    }
}

void PerfilesTab::RenderProfileCard(const profiles::Profile& profile) {
    std::optional<std::string> activeId = profileManager_.GetActiveProfileId();
    bool isActive = activeId.has_value() && *activeId == profile.id;

    ImGui::PushID(profile.id.c_str());
    ImGui::BeginGroup();

    // Hover-test the card's rect before BeginChild (a plain child window
    // isn't a button, so there's no IsItemHovered() for it until after the
    // fact) -- lets the border/background tint react on mouse-over the same
    // way the active-profile tint does.
    ImVec2 cardMin = ImGui::GetCursorScreenPos();
    ImVec2 cardMax = ImVec2(cardMin.x + kCardWidth, cardMin.y + kCardHeight);
    bool isHovered = !isActive && ImGui::IsMouseHoveringRect(cardMin, cardMax);

    int pushedColors = 0;
    if (isActive) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                               ImVec4(Theme::kAccent.x, Theme::kAccent.y, Theme::kAccent.z, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::kAccent);
        pushedColors = 2;
    } else if (isHovered) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(Theme::kAccentHover.x, Theme::kAccentHover.y,
                                                         Theme::kAccentHover.z, 0.10f));
        ImGui::PushStyleColor(ImGuiCol_Border, Theme::kAccentHover);
        pushedColors = 2;
    }

    ImGui::BeginChild(profile.id.c_str(), ImVec2(kCardWidth, kCardHeight), ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoScrollbar);
    if (Theme::g_fonts.body) {
        ImGui::PushFont(Theme::g_fonts.body);
    }
    // Just the name, centered both horizontally and vertically in the card
    // (no separate icon label above it anymore). A "/" splits into two
    // lines (see CenteredProfileName), so the vertical-centering estimate
    // accounts for that instead of measuring a single wrapped block.
    bool nameHasTwoLines = profile.name.find('/') != std::string::npos;
    float nameHeight = nameHasTwoLines
                            ? ImGui::GetTextLineHeight() * 2.0f
                            : ImGui::CalcTextSize(profile.name.c_str(), nullptr, false, kCardWidth - kCardPadding).y;
    float verticalOffset = (kCardHeight - nameHeight) * 0.5f;
    if (verticalOffset > 0.0f) {
        ImGui::SetCursorPosY(verticalOffset);
    }
    CenteredProfileName(profile.name);
    if (Theme::g_fonts.body) {
        ImGui::PopFont();
    }
    if (profile.isPredefined) {
        // Pinned to the card's bottom edge (rather than stacked right after
        // the name) so it's always inside the fixed-height card regardless
        // of whether the name above wrapped to one or two lines.
        ImGui::SetCursorPosY(kCardHeight - ImGui::GetTextLineHeight() - kCardPadding * 0.5f);
        CenteredText("Bloqueado", /*disabled=*/true);
    }
    ImGui::EndChild();

    if (pushedColors > 0) {
        ImGui::PopStyleColor(pushedColors);
    }

    // Non-interactive content inside the child (plain text only), so the
    // child window itself registers as the clickable/hoverable item here.
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", BuildProfileTooltip(profile.vars).c_str());
    }
    if (ImGui::IsItemClicked()) {
        lastApplyResults_ = profileManager_.ApplyProfile(profile.id);
    }

    if (isActive) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        ImU32 accentColor = ImGui::GetColorU32(Theme::kAccent);
        drawList->AddRectFilled(itemMin, ImVec2(itemMin.x + 3.0f, itemMax.y), accentColor);
    }

    if (!profile.isPredefined) {
        if (ImGui::SmallButton("Editar")) {
            profileEditorDialog_.OpenForEdit(profile);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Borrar")) {
            profileManager_.DeleteProfile(profile.id);
        }
    }

    ImGui::EndGroup();
    ImGui::PopID();

    float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float lastItemX2 = ImGui::GetItemRectMax().x;
    float nextItemX2 = lastItemX2 + ImGui::GetStyle().ItemSpacing.x + kCardWidth;
    if (nextItemX2 < windowVisibleX2) {
        ImGui::SameLine();
    }
}

void PerfilesTab::RenderApplyFeedback() {
    bool hasIssues = false;
    for (const profiles::AppliedVariableResult& result : lastApplyResults_) {
        if (result.result != profiles::ApplyResult::Ok) {
            hasIssues = true;
            break;
        }
    }
    if (!hasIssues) {
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    for (const profiles::AppliedVariableResult& result : lastApplyResults_) {
        if (result.result == profiles::ApplyResult::Ok) {
            continue;
        }
        ImGui::TextDisabled("%s: %s", VariableDisplayName(result.variableName),
                             ApplyResultLabel(result.result));
    }
}

std::string PerfilesTab::DescribeRule(const profiles::AutomationRule& rule) const {
    std::string targetName = "(perfil eliminado)";
    for (const profiles::Profile& profile : profileManager_.GetPredefinedProfiles()) {
        if (profile.id == rule.targetProfileId) {
            targetName = profile.name;
            break;
        }
    }
    if (targetName == "(perfil eliminado)") {
        for (const profiles::Profile& profile : profileManager_.GetCustomProfiles()) {
            if (profile.id == rule.targetProfileId) {
                targetName = profile.name;
                break;
            }
        }
    }

    char buffer[160];
    switch (rule.type) {
        case profiles::RuleTriggerType::TimeWindow: {
            int startHour = rule.startMinuteOfDay / 60;
            int startMinute = rule.startMinuteOfDay % 60;
            int endHour = rule.endMinuteOfDay / 60;
            int endMinute = rule.endMinuteOfDay % 60;
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d-%02d:%02d%s -> %s", startHour, startMinute,
                          endHour, endMinute, rule.weekdaysOnly ? " (entre semana)" : "", targetName.c_str());
            break;
        }
        case profiles::RuleTriggerType::BatteryBelow:
            std::snprintf(buffer, sizeof(buffer), "Bateria < %d%% -> %s", rule.batteryThresholdPercent,
                          targetName.c_str());
            break;
        case profiles::RuleTriggerType::PowerSourceChange:
            std::snprintf(buffer, sizeof(buffer), "%s -> %s",
                          rule.triggerOnAcToBattery ? "Corriente -> Bateria" : "Bateria -> Corriente",
                          targetName.c_str());
            break;
    }
    return std::string(buffer);
}

void PerfilesTab::RenderAutomationSection() {
    ImGui::Dummy(ImVec2(0.0f, 12.0f));

    // Bordered panel like HardwareMonitorTab's "Fan Speed" card, so this
    // reads as its own titled panel instead of a bare, frameless list.
    ImGui::BeginChild("AutomationCard", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    if (RenderSectionHeaderAddButton("AUTOMATIZACION")) {
        ruleEditorDialog_.OpenForCreate();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Snapshot copy, same reasoning as customProfilesSnapshot above -- a
    // Delete/Move call triggered mid-loop must not invalidate this iteration.
    std::vector<profiles::AutomationRule> rulesSnapshot = automationEngine_.GetRules();

    for (size_t i = 0; i < rulesSnapshot.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));

        bool enabled = rulesSnapshot[i].enabled;
        if (ImGui::Checkbox("##enabled", &enabled)) {
            profiles::AutomationRule updated = rulesSnapshot[i];
            updated.enabled = enabled;
            automationEngine_.UpdateRule(rulesSnapshot[i].id, updated);
        }

        ImGui::SameLine();
        std::string description = DescribeRule(rulesSnapshot[i]);
        // AllowOverlap: this Selectable defaults to filling the rest of the
        // line's width (size.x==0), which extends underneath the "Borrar"
        // button drawn further down via SameLine() -- without this flag,
        // Dear ImGui gives exclusive hover to whichever item claims a given
        // screen position first, so the button drawn on top would never
        // receive hover/clicks at all.
        ImGui::Selectable(description.c_str(), false, ImGuiSelectableFlags_AllowOverlap);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("PERFILES_RULE_ROW", &i, sizeof(size_t));
            ImGui::TextUnformatted(description.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PERFILES_RULE_ROW")) {
                size_t srcIndex = *static_cast<const size_t*>(payload->Data);
                automationEngine_.MoveRule(srcIndex, i);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Borrar")) {
            automationEngine_.DeleteRule(rulesSnapshot[i].id);
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

} // namespace ui
