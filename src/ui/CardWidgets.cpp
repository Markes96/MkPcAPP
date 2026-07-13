#include "CardWidgets.h"
#include "Theme.h"
#include <imgui.h>

namespace ui {

void CenteredText(const char* text, bool disabled) {
    float availWidth = ImGui::GetContentRegionAvail().x;
    float textWidth = ImGui::CalcTextSize(text).x;
    if (textWidth < availWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - textWidth) * 0.5f);
    }
    if (disabled) {
        ImGui::TextDisabled("%s", text);
    } else {
        ImGui::TextUnformatted(text);
    }
}

bool RenderSectionHeaderAddButton(const char* headerText) {
    if (Theme::g_fonts.value) {
        ImGui::PushFont(Theme::g_fonts.value);
    }
    ImGui::TextUnformatted(headerText);
    float headerHeight = ImGui::GetItemRectSize().y;
    if (Theme::g_fonts.value) {
        ImGui::PopFont();
    }

    ImGui::SameLine();
    // SmallButton forces FramePadding.y to 0 internally, so its real height
    // is just the text line height -- GetFrameHeight() (which assumes the
    // style's normal, non-zero FramePadding) overstates it and under-shifts
    // the button, leaving it sitting above center against the header text.
    float offset = (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
    if (offset > 0.0f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, Theme::kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    bool clicked = ImGui::SmallButton("+");
    ImGui::PopStyleColor(4);
    return clicked;
}

} // namespace ui
