#include "ConfirmDeleteGroupDialog.h"
#include <imgui.h>

namespace ui {

namespace {
constexpr const char* kPopupId = "Eliminar grupo###ConfirmDeleteGroupDialog";
} // namespace

void ConfirmDeleteGroupDialog::OpenForGroup(const groups::LaunchGroup& group) {
    pendingGroup_ = group;
    isOpen_ = true;
    openRequested_ = true;
}

void ConfirmDeleteGroupDialog::Render(groups::GroupManager& manager, groups::GroupLauncher& launcher,
                                       const std::unordered_set<std::string>& openGroupIds, uint64_t tickCount) {
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

    ImGui::TextWrapped("Seguro que quieres eliminar el grupo \"%s\"?", pendingGroup_.name.c_str());
    ImGui::TextWrapped("Esto no cierra ninguna app que otro grupo siga usando -- solo borra la definicion del grupo.");

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    if (ImGui::Button("Eliminar")) {
        if (openGroupIds.count(pendingGroup_.id) > 0) {
            launcher.CloseGroup(pendingGroup_.id, pendingGroup_.entries, tickCount);
        }
        manager.DeleteGroup(pendingGroup_.id);
        justDeletedId_ = pendingGroup_.id;
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

std::optional<std::string> ConfirmDeleteGroupDialog::ConsumeJustDeleted() {
    std::optional<std::string> result = justDeletedId_;
    justDeletedId_.reset();
    return result;
}

} // namespace ui
