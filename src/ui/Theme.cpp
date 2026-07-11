#include "Theme.h"
#include <implot.h>
#include <windows.h>
#include <string>

namespace ui::Theme {

namespace {

std::wstring FontPath(const wchar_t* fileName) {
    wchar_t windowsDir[MAX_PATH];
    UINT len = GetWindowsDirectoryW(windowsDir, MAX_PATH);
    std::wstring base = (len > 0 && len < MAX_PATH) ? windowsDir : L"C:\\Windows";
    return base + L"\\Fonts\\" + fileName;
}

bool FileExists(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

ImVec4 Col(int r, int g, int b, int a = 255) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

} // namespace

Fonts LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    Fonts fonts;

    std::wstring regularPath = FontPath(L"segoeui.ttf");
    std::wstring semiboldPath = FontPath(L"seguisb.ttf"); // Segoe UI Semibold

    if (FileExists(regularPath)) {
        std::string regularPathUtf8(regularPath.begin(), regularPath.end());
        fonts.body = io.Fonts->AddFontFromFileTTF(regularPathUtf8.c_str(), 16.0f);
    }

    std::wstring valueFontPath = FileExists(semiboldPath) ? semiboldPath : regularPath;
    if (FileExists(valueFontPath)) {
        std::string valuePathUtf8(valueFontPath.begin(), valueFontPath.end());
        fonts.value = io.Fonts->AddFontFromFileTTF(valuePathUtf8.c_str(), 26.0f);
    }

    // If nothing loaded (stripped-down Windows install without these files),
    // ImGui automatically falls back to its built-in font at Build() time.
    return fonts;
}

void Apply() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Mostly rectangular, low rounding, no heavy borders — matches the flat
    // VS-Code look rather than ImGui's default rounded/bordered widgets.
    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(16, 16);
    style.FramePadding = ImVec2(8, 6);
    style.ItemSpacing = ImVec2(10, 10);
    style.ScrollbarSize = 12.0f;
    style.IndentSpacing = 16.0f;

    ImVec4 bg = Col(0x1e, 0x1e, 0x1e);
    ImVec4 panel = Col(0x25, 0x25, 0x26);
    ImVec4 panelHover = Col(0x2d, 0x2d, 0x2e);
    ImVec4 panelActive = Col(0x35, 0x35, 0x37);
    ImVec4 border = Col(0x3c, 0x3c, 0x3c);
    ImVec4 text = Col(0xd4, 0xd4, 0xd4);
    ImVec4 textMuted = Col(0x8a, 0x8a, 0x8a);
    ImVec4 scrollbarBg = Col(0x1e, 0x1e, 0x1e);
    ImVec4 scrollbarGrab = Col(0x42, 0x42, 0x42);
    ImVec4 tableRowAlt = Col(0x22, 0x22, 0x22, 0x80);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textMuted;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = panel;
    colors[ImGuiCol_PopupBg] = panel;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = panelHover;
    colors[ImGuiCol_FrameBgHovered] = panelActive;
    colors[ImGuiCol_FrameBgActive] = panelActive;
    colors[ImGuiCol_TitleBg] = bg;
    colors[ImGuiCol_TitleBgActive] = bg;
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg] = panel;
    colors[ImGuiCol_ScrollbarBg] = scrollbarBg;
    colors[ImGuiCol_ScrollbarGrab] = scrollbarGrab;
    colors[ImGuiCol_ScrollbarGrabHovered] = Col(0x4f, 0x4f, 0x4f);
    colors[ImGuiCol_ScrollbarGrabActive] = Col(0x5a, 0x5a, 0x5a);
    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kAccent;
    colors[ImGuiCol_SliderGrabActive] = kAccentHover;
    colors[ImGuiCol_Button] = panelHover;
    colors[ImGuiCol_ButtonHovered] = panelActive;
    colors[ImGuiCol_ButtonActive] = Col(0x3d, 0x3d, 0x40);
    colors[ImGuiCol_Header] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
    colors[ImGuiCol_HeaderActive] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.38f);
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = kAccent;
    colors[ImGuiCol_SeparatorActive] = kAccent;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = kAccent;
    colors[ImGuiCol_ResizeGripActive] = kAccentHover;
    colors[ImGuiCol_Tab] = panel;
    colors[ImGuiCol_TabHovered] = panelActive;
    colors[ImGuiCol_TabActive] = panelActive;
    colors[ImGuiCol_TabUnfocused] = panel;
    colors[ImGuiCol_TabUnfocusedActive] = panelHover;
    colors[ImGuiCol_PlotLines] = kAccent;
    colors[ImGuiCol_PlotLinesHovered] = kAccentHover;
    colors[ImGuiCol_PlotHistogram] = kAccent;
    colors[ImGuiCol_PlotHistogramHovered] = kAccentHover;
    colors[ImGuiCol_TableHeaderBg] = panelHover;
    colors[ImGuiCol_TableBorderStrong] = border;
    colors[ImGuiCol_TableBorderLight] = border;
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = tableRowAlt;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_NavHighlight] = kAccent;
}

void ApplyPlotStyle() {
    ImPlotStyle& style = ImPlot::GetStyle();
    style.PlotPadding = ImVec2(8, 8);
    style.LegendPadding = ImVec2(6, 4);
    style.PlotBorderSize = 0.0f;
    style.PlotMinSize = ImVec2(50, 50);

    ImVec4 bg = Col(0x1e, 0x1e, 0x1e);
    ImVec4 panel = Col(0x25, 0x25, 0x26);
    ImVec4 grid = Col(0x3c, 0x3c, 0x3c, 0x80);

    ImVec4* colors = style.Colors;
    colors[ImPlotCol_FrameBg] = panel;
    colors[ImPlotCol_PlotBg] = panel;
    colors[ImPlotCol_PlotBorder] = ImVec4(0, 0, 0, 0);
    colors[ImPlotCol_LegendBg] = panel;
    colors[ImPlotCol_LegendBorder] = ImVec4(0, 0, 0, 0);
    colors[ImPlotCol_AxisGrid] = grid;
    colors[ImPlotCol_AxisText] = Col(0x8a, 0x8a, 0x8a);

    // Two-color line palette so every graph's pair of series stays consistent
    // with the grayscale + single-accent theme instead of ImPlot's default
    // rainbow colormap. Blue + amber rather than blue + teal: the previous
    // teal sat too close to the accent blue to tell the two series apart at a
    // glance.
    static const ImVec4 kSeriesColors[2] = {
        kAccent,               // primary series: accent blue
        Col(0xe0, 0xa1, 0x58), // secondary series: warm amber
    };
    static ImPlotColormap colormap =
        ImPlot::AddColormap("MkPCApp", kSeriesColors, 2);
    style.Colormap = colormap;
}

} // namespace ui::Theme
