#include "ProfileEditorDialog.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>

namespace ui {

namespace {

constexpr const char* kPopupId = "Editar perfil###ProfileEditorDialog";

void CopyToBuffer(char* buffer, size_t bufferSize, const std::string& value) {
    std::strncpy(buffer, value.c_str(), bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
}

// Small "(?)" marker with a hover tooltip -- keeps the field's own label
// short while still explaining anything non-obvious (e.g. "0 = nunca").
void HelpMarker(const char* description) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// A plain keyboard-editable number field with a step of one and a Ctrl+click
// fast-step of ten (ImGui::InputInt's built-in +/- buttons), clamped to
// [minValue, maxValue] after every edit -- deliberately not a slider, since
// dragging a slider precisely to an exact value is fiddly compared to typing
// it or nudging it with the arrows.
void ClampedInputInt(const char* label, int* value, int minValue, int maxValue, int step = 1,
                      int stepFast = 10) {
    ImGui::InputInt(label, value, step, stepFast);
    *value = std::clamp(*value, minValue, maxValue);
}

// Same idiom as ClampedInputInt, but for a *Sec field the app stores in
// seconds (Windows' power-timeout APIs are seconds-based) that reads better
// to a user in minutes -- edits the field as whole minutes and converts back
// to seconds on every change, rather than storing minutes and converting at
// every call site that talks to Windows.
void ClampedMinutesInput(const char* label, int* secondsValue, int minMinutes, int maxMinutes, int step = 1,
                          int stepFast = 5) {
    int minutes = *secondsValue / 60;
    ImGui::InputInt(label, &minutes, step, stepFast);
    minutes = std::clamp(minutes, minMinutes, maxMinutes);
    *secondsValue = minutes * 60;
}

} // namespace

void ProfileEditorDialog::OpenForCreate(const profiles::ProfileManager& profileManager) {
    isOpen_ = true;
    isEditMode_ = false;
    editingProfileId_.clear();

    // Sensible starting point: the real "Equilibrado" predefined profile's
    // current values, looked up instead of hand-duplicated here, so this
    // can't silently drift if BuildPredefinedProfiles() is ever tuned.
    std::optional<profiles::Profile> equilibrado = profileManager.GetProfileById("predef.equilibrado");
    vars_ = equilibrado.has_value() ? equilibrado->vars : profiles::ProfileVariables{};

    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), "Mi perfil");
    CopyToBuffer(iconBuffer_, sizeof(iconBuffer_), "");

    openRequested_ = true;
}

void ProfileEditorDialog::OpenForEdit(const profiles::Profile& profile) {
    isOpen_ = true;
    isEditMode_ = true;
    editingProfileId_ = profile.id;
    vars_ = profile.vars;

    CopyToBuffer(nameBuffer_, sizeof(nameBuffer_), profile.name);
    CopyToBuffer(iconBuffer_, sizeof(iconBuffer_), profile.icon);

    openRequested_ = true;
}

void ProfileEditorDialog::Render(profiles::ProfileManager& profileManager) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::SeparatorText("General");
    ImGui::InputText("Nombre", nameBuffer_, sizeof(nameBuffer_));

    ImGui::SeparatorText("Energia");

    int powerPlanIndex = static_cast<int>(vars_.powerPlan);
    const char* powerPlanItems[] = {"Ahorro", "Equilibrado", "Alto rendimiento", "Maximo rendimiento"};
    if (ImGui::Combo("Plan de energia", &powerPlanIndex, powerPlanItems, IM_ARRAYSIZE(powerPlanItems))) {
        vars_.powerPlan = static_cast<profiles::PowerPlan>(powerPlanIndex);
    }

    ClampedMinutesInput("Apagado de pantalla, con corriente (min.)", &vars_.screenOffTimeoutAcSec, 0, 60);
    HelpMarker("0 = nunca apagar la pantalla automaticamente.");
    ClampedMinutesInput("Apagado de pantalla, con bateria (min.)", &vars_.screenOffTimeoutDcSec, 0, 60);
    HelpMarker("0 = nunca apagar la pantalla automaticamente.");
    ClampedMinutesInput("Suspension, con corriente (min.)", &vars_.sleepTimeoutAcSec, 0, 120, 1, 10);
    HelpMarker("0 = nunca suspender el equipo automaticamente.");
    ClampedMinutesInput("Suspension, con bateria (min.)", &vars_.sleepTimeoutDcSec, 0, 120, 1, 10);
    HelpMarker("0 = nunca suspender el equipo automaticamente.");
    ClampedMinutesInput("Hibernacion, con corriente (min.)", &vars_.hibernateTimeoutAcSec, 0, 120, 1, 10);
    HelpMarker("0 = nunca hibernar el equipo automaticamente.");
    ClampedMinutesInput("Hibernacion, con bateria (min.)", &vars_.hibernateTimeoutDcSec, 0, 120, 1, 10);
    HelpMarker("0 = nunca hibernar el equipo automaticamente.");

    ImGui::SeparatorText("Pantalla");
    ClampedInputInt("Brillo (%)", &vars_.brightnessPercent, 1, 100);

    ImGui::SeparatorText("Sonido");
    ImGui::TextDisabled("Solo se aplica si activas su casilla \"Modificar\".");

    ImGui::Checkbox("Modificar volumen", &vars_.volumePercent.apply);
    ImGui::BeginDisabled(!vars_.volumePercent.apply);
    ClampedInputInt("Volumen (%)##VolumeValue", &vars_.volumePercent.value, 0, 100);
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ImGui::Button("Guardar")) {
        std::string name = nameBuffer_;
        std::string icon = iconBuffer_;
        if (isEditMode_) {
            profileManager.UpdateProfile(editingProfileId_, name, icon, vars_);
        } else {
            profileManager.CreateProfile(name, icon, vars_);
        }
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

} // namespace ui
