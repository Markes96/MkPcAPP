#pragma once

namespace ui {

// Shared fixed card width used by every tab that lays out a flowing grid of
// bordered cards (PerfilesTab, StartupTab) -- keeps their visual language
// consistent without each tab re-declaring the same pixel value. Card
// height is deliberately not shared: it varies with how much each tab's
// card content needs.
constexpr float kCardWidth = 160.0f;

// Centers a single line of text horizontally within the current content
// region (falls back to left-aligned if it doesn't fit). Shared between
// PerfilesTab and StartupTab, both of which lay out fixed-size cards with
// centered labels.
void CenteredText(const char* text, bool disabled = false);

// Blue "+" button (Visual Studio accent style) used to open a create/add
// dialog next to a section header, vertically centered against the
// header's (larger/bold) font instead of just sharing its line top.
bool RenderSectionHeaderAddButton(const char* headerText);

} // namespace ui
