#pragma once
#include <cstdint>

namespace ui {

// Extensibility point: future iterations add a new tab by implementing this
// interface and registering one instance with TabManager. No other app-shell
// code needs to change.
class ITab {
public:
    virtual ~ITab() = default;

    virtual const char* GetTitle() const = 0;

    // Short label (1-2 characters) shown in the sidebar icon button; the full
    // title shows as a tooltip on hover. Plain text rather than a symbol font,
    // so it always renders correctly regardless of glyph coverage.
    virtual const char* GetIcon() const = 0;

    // Called every frame while this tab is the active tab and the window is
    // visible. Draw ImGui widgets directly inside the current window/tab item.
    virtual void OnRender(float deltaTimeSeconds) = 0;

    // Called once per second on the data-tick thread regardless of which tab is
    // active or whether the window is visible, so background tabs can keep
    // collecting data. Optional — most tabs won't need it directly if they read
    // from a shared aggregator instead.
    virtual void OnTick(uint64_t tickMs) {}
};

} // namespace ui
