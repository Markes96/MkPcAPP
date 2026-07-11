#pragma once
#include <imgui.h>

namespace ui::Theme {

// Fonts loaded from the system (Segoe UI) instead of ImGui's built-in bitmap
// font — that default font is the single biggest reason the app read as a
// "console with graphs" rather than a modern dashboard.
struct Fonts {
    ImFont* body = nullptr;  // Segoe UI Regular — labels, general UI text
    ImFont* value = nullptr; // Segoe UI Semibold, larger — big stat numbers
};

// Set once by LoadFonts() during Application::Init, read by any UI code on the
// render thread. A plain global is fine here: single-threaded UI, single app.
inline Fonts g_fonts;

// VS-Code-inspired dark gray palette: grayscale panels, one accent color
// (blue) reserved for active/highlighted state, progress bars and chart
// lines. Mostly rectangular (low corner rounding), matching the reference look.
constexpr ImVec4 kAccent = ImVec4(0.216f, 0.580f, 1.0f, 1.0f);
constexpr ImVec4 kAccentHover = ImVec4(0.31f, 0.66f, 1.0f, 1.0f);

void Apply();

// Loads Segoe UI (Regular + Semibold) from the system Fonts folder at two
// sizes. Falls back to ImGui's default font if the files aren't found (e.g. a
// stripped-down Windows install) — never fails outright.
Fonts LoadFonts();

// Registers a 2-color line colormap (accent blue + a muted teal) so every
// ImPlot graph's two series look consistent instead of ImPlot's default
// multi-color palette clashing with the grayscale theme. Call once after
// ImPlot::CreateContext().
void ApplyPlotStyle();

} // namespace ui::Theme
