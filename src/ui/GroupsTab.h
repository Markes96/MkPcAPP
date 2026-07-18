#pragma once
#include "ITab.h"
#include "ConfirmDeleteGroupDialog.h"
#include "GroupEditorDialog.h"
#include "../groups/GroupLauncher.h"
#include "../groups/GroupManager.h"
#include "../groups/GroupProcessTracker.h"
#include "../platform/DX11Renderer.h"
#include "../platform/IconTextureCache.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ui {

// The "Grupos" section: user-defined groups of apps (e.g. a game plus
// Discord plus an overlay) opened/closed together with one click each.
// See docs/superpowers/specs/2026-07-18-grupos-lanzamiento-design.md.
class GroupsTab : public ITab {
public:
    explicit GroupsTab(platform::DX11Renderer& renderer);

    const char* GetTitle() const override { return "Grupos"; }
    const char* GetIcon() const override { return "G"; }
    void OnRender(float deltaTimeSeconds) override;
    void OnTick(uint64_t tickMs) override;

private:
    void RenderGroupCard(const groups::LaunchGroup& group);

    groups::GroupManager manager_;
    groups::GroupProcessTracker tracker_;
    groups::GroupLauncher launcher_{tracker_};
    platform::IconTextureCache iconCache_; // only ever touched from OnRender (render thread)

    ui::GroupEditorDialog editorDialog_;
    ui::ConfirmDeleteGroupDialog confirmDeleteDialog_;

    // Render-thread-only state (button clicks): which groups are
    // currently "open", and the per-entry outcome of each group's last
    // "Abrir grupo" click (for the "ya estaba abierto" note).
    std::unordered_set<std::string> openGroupIds_;
    std::unordered_map<std::string, std::vector<groups::EntryOpenOutcome>> lastOpenOutcomes_;

    std::atomic<uint64_t> tickCounter_{0};

    std::mutex errorMutex_; // guards lastErrorMessage_ (written from OnTick, read from OnRender)
    std::string lastErrorMessage_;
};

} // namespace ui
