#include "RuleEditorDialog.h"
#include <imgui.h>
#include <algorithm>
#include <vector>

namespace ui {

namespace {

constexpr const char* kPopupId = "Nueva regla###RuleEditorDialog";

// A plain keyboard-editable number field (InputInt's built-in +/- buttons,
// one step at a time), clamped to [minValue, maxValue] -- not a slider, same
// reasoning as ProfileEditorDialog's ClampedInputInt: typing/nudging to an
// exact value beats dragging a slider to it.
void ClampedInputInt(const char* label, int* value, int minValue, int maxValue) {
    ImGui::InputInt(label, value, 1, 1);
    *value = std::clamp(*value, minValue, maxValue);
}

// Compact "HH : MM" digital-clock style pair of fields instead of two
// separate labeled sliders. Plain typable number fields with no +/- step
// buttons (step=0) -- a real clock face doesn't have spin buttons bolted
// onto it, and showing "[-][23][+] : [-][00][+]" looked cluttered rather
// than clock-like; type the two digits directly instead. Label drawn after
// the fields (via InputInt's own trailing label), matching every other
// field in this app rather than a standalone label before the widget.
void TimeOfDayInput(const char* label, int* hour, int* minute) {
    ImGui::PushID(label);
    ImGui::PushItemWidth(30.0f);
    // InputInt has no format parameter -- InputScalar with "%02d" is the
    // idiom for zero-padded digits (so "6" reads as "06", matching a real
    // clock face instead of looking like a typo/truncated value).
    ImGui::InputScalar("##Hour", ImGuiDataType_S32, hour, nullptr, nullptr, "%02d");
    *hour = std::clamp(*hour, 0, 23);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(":");
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::InputScalar("##Minute", ImGuiDataType_S32, minute, nullptr, nullptr, "%02d");
    *minute = std::clamp(*minute, 0, 59);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    ImGui::PopID();
}

} // namespace

void RuleEditorDialog::OpenForCreate() {
    isOpen_ = true;
    rule_ = profiles::AutomationRule{};
    startHour_ = 23;
    startMinute_ = 0;
    endHour_ = 7;
    endMinute_ = 0;
    targetProfileIndex_ = 0;

    openRequested_ = true;
}

void RuleEditorDialog::Render(profiles::AutomationEngine& automationEngine,
                               profiles::ProfileManager& profileManager) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    int typeIndex = static_cast<int>(rule_.type);
    const char* typeItems[] = {"Franja horaria", "Bateria por debajo de", "Cambio de fuente de alimentacion"};
    if (ImGui::Combo("Disparador", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems))) {
        rule_.type = static_cast<profiles::RuleTriggerType>(typeIndex);
    }

    if (rule_.type == profiles::RuleTriggerType::TimeWindow) {
        TimeOfDayInput("Desde", &startHour_, &startMinute_);
        TimeOfDayInput("Hasta", &endHour_, &endMinute_);
        ImGui::Checkbox("Solo entre semana", &rule_.weekdaysOnly);
    } else if (rule_.type == profiles::RuleTriggerType::BatteryBelow) {
        ClampedInputInt("Umbral de bateria (%)", &rule_.batteryThresholdPercent, 0, 100);
    } else {
        int directionIndex = rule_.triggerOnAcToBattery ? 0 : 1;
        const char* directionItems[] = {"Corriente -> Bateria", "Bateria -> Corriente"};
        if (ImGui::Combo("Direccion", &directionIndex, directionItems, IM_ARRAYSIZE(directionItems))) {
            rule_.triggerOnAcToBattery = (directionIndex == 0);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Build the combined predefined + custom profile list for the target combo.
    // customProfilesSnapshot must outlive allProfiles (both are local to this
    // call) -- GetCustomProfiles() returns by value, so taking &p straight
    // from its range-for would point into a temporary destroyed at the end
    // of that loop, leaving every custom-profile pointer dangling for the
    // rest of this function (blank name in the combo, garbage id on save).
    std::vector<profiles::Profile> customProfilesSnapshot = profileManager.GetCustomProfiles();
    std::vector<const profiles::Profile*> allProfiles;
    for (const profiles::Profile& p : profileManager.GetPredefinedProfiles()) {
        allProfiles.push_back(&p);
    }
    for (const profiles::Profile& p : customProfilesSnapshot) {
        allProfiles.push_back(&p);
    }

    if (targetProfileIndex_ >= static_cast<int>(allProfiles.size())) {
        targetProfileIndex_ = 0;
    }

    if (!allProfiles.empty()) {
        std::vector<const char*> profileNames;
        profileNames.reserve(allProfiles.size());
        for (const profiles::Profile* p : allProfiles) {
            profileNames.push_back(p->name.c_str());
        }
        ImGui::Combo("Perfil objetivo", &targetProfileIndex_, profileNames.data(),
                      static_cast<int>(profileNames.size()));
    } else {
        ImGui::TextDisabled("No hay perfiles disponibles");
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    bool canSave = !allProfiles.empty();
    if (!canSave) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Guardar")) {
        rule_.startMinuteOfDay = startHour_ * 60 + startMinute_;
        rule_.endMinuteOfDay = endHour_ * 60 + endMinute_;
        rule_.targetProfileId = allProfiles[static_cast<size_t>(targetProfileIndex_)]->id;
        rule_.enabled = true;
        automationEngine.AddRule(rule_);
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    if (!canSave) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace ui
