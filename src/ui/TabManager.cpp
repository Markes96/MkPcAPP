#include "TabManager.h"
#include "Theme.h"
#include <imgui.h>

namespace ui {

namespace {
constexpr float kSidebarWidth = 56.0f;
constexpr float kIconButtonSize = 40.0f;
} // namespace

void TabManager::RenderSidebarButton(size_t index) {
    ITab* tab = tabs_[index].get();
    bool isActive = (index == activeIndex_);

    ImGui::PushID(static_cast<int>(index));
    ImGui::SetCursorPosX((kSidebarWidth - kIconButtonSize) * 0.5f);

    ImVec4 activeBg = ImVec4(Theme::kAccent.x, Theme::kAccent.y, Theme::kAccent.z, 0.18f);
    ImVec4 transparent = ImVec4(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Button, isActive ? activeBg : transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.14f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    if (ImGui::Button(tab->GetIcon(), ImVec2(kIconButtonSize, kIconButtonSize))) {
        activeIndex_ = index;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tab->GetTitle());
    }

    if (isActive) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        ImU32 accentColor = ImGui::GetColorU32(Theme::kAccent);
        drawList->AddRectFilled(ImVec2(0, itemMin.y), ImVec2(3, itemMax.y), accentColor);
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, 4));
}

void TabManager::Render(float deltaTimeSeconds) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("MkPCAppMainWindow", nullptr, flags);
    ImGui::PopStyleVar();

    ImVec4 sidebarBg(0.20f, 0.20f, 0.20f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sidebarBg);
    ImGui::BeginChild("Sidebar", ImVec2(kSidebarWidth, 0), false);
    ImGui::Dummy(ImVec2(0, 8));
    for (size_t i = 0; i < tabs_.size(); ++i) {
        RenderSidebarButton(i);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 0);

    // ImGuiChildFlags_AlwaysUseWindowPadding: without a flag that enables
    // padding, BeginChild ignores the pushed WindowPadding entirely for a
    // borderless child (by design — "no padding by default for non-bordered
    // child windows"), which is why content was flush against the window/
    // sidebar edges despite the style push below.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32, 28));
    ImGui::BeginChild("Content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (activeIndex_ < tabs_.size()) {
        tabs_[activeIndex_]->OnRender(deltaTimeSeconds);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

} // namespace ui
