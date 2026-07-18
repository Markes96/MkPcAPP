#include "GroupsTab.h"
#include "CardWidgets.h"
#include "../platform/StringConvert.h"
#include <imgui.h>
#include <mutex>

namespace ui {

namespace {

constexpr float kIconSize = 28.0f;

// Same "filename without extension" idiom already used independently in
// AddStartupEntryDialog.cpp and ShortcutStartupControl.cpp -- small
// enough, and specific enough to this display purpose, that a shared
// helper isn't worth it (see MiniJson for the bar this codebase actually
// uses to justify extracting a shared module: real duplication of
// non-trivial logic, not a three-line path utility).
std::string EntryDisplayName(const groups::LaunchEntry& entry) {
    const std::string& path = entry.resolvedExePath.empty() ? entry.path : entry.resolvedExePath;
    size_t lastSlash = path.find_last_of("\\/");
    std::string fileName = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
    size_t lastDot = fileName.find_last_of('.');
    return (lastDot == std::string::npos) ? fileName : fileName.substr(0, lastDot);
}

} // namespace

GroupsTab::GroupsTab(platform::DX11Renderer& renderer) : iconCache_(renderer) {
    manager_.Init();
}

void GroupsTab::OnTick(uint64_t /*tickMs*/) {
    uint64_t tick = tickCounter_.fetch_add(1) + 1;
    tracker_.Tick(tick);

    std::vector<std::wstring> failures = tracker_.ConsumeCloseFailures();
    if (!failures.empty()) {
        std::string message = "No se pudo cerrar: ";
        for (size_t i = 0; i < failures.size(); ++i) {
            if (i > 0) {
                message += ", ";
            }
            message += platform::WideToUtf8(failures[i]);
        }
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastErrorMessage_ = message;
    }
}

void GroupsTab::OnRender(float /*deltaTimeSeconds*/) {
    if (RenderSectionHeaderAddButton("GRUPOS")) {
        editorDialog_.OpenForCreate();
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Snapshot copy, same reasoning as PerfilesTab's customProfilesSnapshot:
    // RenderGroupCard can trigger manager_.DeleteGroup() (via the confirm
    // dialog), which would invalidate a live reference mid-loop.
    std::vector<groups::LaunchGroup> groupsSnapshot = manager_.GetGroups();
    if (groupsSnapshot.empty()) {
        ImGui::TextDisabled("No hay grupos creados todavia.");
    } else {
        for (const groups::LaunchGroup& group : groupsSnapshot) {
            RenderGroupCard(group);
        }
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        if (!lastErrorMessage_.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextDisabled("%s", lastErrorMessage_.c_str());
        }
    }

    editorDialog_.Render(manager_);
    editorDialog_.ConsumeJustSaved(); // groupsSnapshot is re-read from manager_ next frame regardless

    confirmDeleteDialog_.Render(manager_, launcher_, openGroupIds_, tickCounter_.load());
    if (std::optional<std::string> deletedId = confirmDeleteDialog_.ConsumeJustDeleted()) {
        openGroupIds_.erase(*deletedId);
        lastOpenOutcomes_.erase(*deletedId);
    }
}

void GroupsTab::RenderGroupCard(const groups::LaunchGroup& group) {
    ImGui::PushID(group.id.c_str());
    bool isOpenState = openGroupIds_.count(group.id) > 0;

    ImGui::BeginChild("GroupCard", ImVec2(-1, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::TextUnformatted(group.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(isOpenState ? "(Abierto)" : "(Cerrado)");

    for (const groups::LaunchEntry& entry : group.entries) {
        bool hasTarget = !entry.resolvedExePath.empty();
        uint64_t textureId = hasTarget ? iconCache_.GetOrCreateTexture(entry.resolvedExePath) : 0;
        if (textureId != 0) {
            ImGui::Image(textureId, ImVec2(kIconSize, kIconSize));
        } else {
            RenderIconPlaceholder(kIconSize);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    auto outcomeIt = lastOpenOutcomes_.find(group.id);
    if (outcomeIt != lastOpenOutcomes_.end()) {
        for (size_t i = 0; i < outcomeIt->second.size() && i < group.entries.size(); ++i) {
            groups::EntryOpenStatus status = outcomeIt->second[i].status;
            if (status == groups::EntryOpenStatus::ExternallyOwned) {
                ImGui::TextDisabled("%s ya estaba abierto -- no se cerrara con este grupo.",
                                     EntryDisplayName(group.entries[i]).c_str());
            } else if (status == groups::EntryOpenStatus::LaunchFailed) {
                // Per-entry visibility for a failed launch (broken .lnk,
                // moved/deleted exe, permissions) -- matches the design
                // spec's error matrix: a failure is shown, never silently
                // swallowed, without aborting the rest of the group.
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No se pudo abrir %s.",
                                    EntryDisplayName(group.entries[i]).c_str());
            }
        }
    }

    ImGui::BeginDisabled(isOpenState);
    if (ImGui::Button("Abrir grupo")) {
        std::vector<groups::EntryOpenOutcome> outcomes = launcher_.OpenGroup(group.id, group.entries);
        bool anyFailed = false;
        for (const groups::EntryOpenOutcome& outcome : outcomes) {
            if (outcome.status == groups::EntryOpenStatus::LaunchFailed) {
                anyFailed = true;
            }
        }
        lastOpenOutcomes_[group.id] = std::move(outcomes);
        openGroupIds_.insert(group.id);

        std::lock_guard<std::mutex> lock(errorMutex_);
        lastErrorMessage_ = anyFailed ? ("No se pudieron abrir todas las apps de \"" + group.name + "\".") : "";
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!isOpenState);
    if (ImGui::Button("Cerrar grupo")) {
        launcher_.CloseGroup(group.id, group.entries, tickCounter_.load());
        openGroupIds_.erase(group.id);
        lastOpenOutcomes_.erase(group.id);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Editar")) {
        editorDialog_.OpenForEdit(group);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Eliminar")) {
        confirmDeleteDialog_.OpenForGroup(group);
    }

    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PopID();
}

} // namespace ui
