#include "ConfirmDeleteDialog.h"
#include <imgui.h>

namespace ui {

namespace {
constexpr const char* kPopupId = "Eliminar del inicio###ConfirmDeleteDialog";
} // namespace

void ConfirmDeleteDialog::OpenForEntry(const startup::StartupEntry& entry) {
    pendingEntry_ = entry;
    errorMessage_.clear();
    isOpen_ = true;
    openRequested_ = true;
}

void ConfirmDeleteDialog::Render(startup::StartupScanner& scanner) {
    if (openRequested_) {
        openRequested_ = false;
        ImGui::OpenPopup(kPopupId);
    }

    if (!isOpen_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(kPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextWrapped("Seguro que quieres eliminar \"%s\" del inicio de Windows?",
                        pendingEntry_.displayName.c_str());
    ImGui::TextWrapped("Esta accion no se puede deshacer desde la app.");

    if (!errorMessage_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", errorMessage_.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ImGui::Button("Eliminar")) {
        if (scanner.DeleteEntry(pendingEntry_)) {
            isOpen_ = false;
            justDeleted_ = true;
            ImGui::CloseCurrentPopup();
        } else {
            errorMessage_ = "No se pudo eliminar \"" + pendingEntry_.displayName + "\".";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar")) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool ConfirmDeleteDialog::ConsumeJustDeleted() {
    bool result = justDeleted_;
    justDeleted_ = false;
    return result;
}

} // namespace ui
