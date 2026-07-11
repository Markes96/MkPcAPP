#pragma once
#include "ITab.h"
#include <memory>
#include <vector>

namespace ui {

// Owns the registered tabs and draws the left icon sidebar + content area.
// Adding a new feature tab in a future iteration means writing an ITab
// subclass and calling RegisterTab once during app startup — nothing here
// needs to change.
class TabManager {
public:
    void RegisterTab(std::unique_ptr<ITab> tab) {
        tabs_.push_back(std::move(tab));
    }

    void OnTick(uint64_t tickMs) {
        for (auto& tab : tabs_) {
            tab->OnTick(tickMs);
        }
    }

    // Call once per frame; must be called between ImGui::NewFrame() and Render().
    void Render(float deltaTimeSeconds);

private:
    void RenderSidebarButton(size_t index);

    std::vector<std::unique_ptr<ITab>> tabs_;
    size_t activeIndex_ = 0;
};

} // namespace ui
