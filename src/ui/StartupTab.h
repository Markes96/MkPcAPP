#pragma once
#include "ITab.h"
#include "AddStartupEntryDialog.h"
#include "../startup/StartupScanner.h"
#include "../platform/IconTextureCache.h"
#include "../platform/DX11Renderer.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace ui {

// The "Inicio" section: lists third-party apps registered to launch with
// Windows (Registry Run under HKCU/HKLM, plus both Startup-folder shortcut
// locations), lets the user enable/disable them (reversible, never
// deletes), and add a new one manually. Apps signed by Microsoft are
// excluded entirely by startup::StartupScanner before this tab ever sees
// them -- this tab never special-cases "critical" apps itself, it only
// renders what the scanner already filtered.
class StartupTab : public ITab {
public:
    StartupTab(HWND hwnd, platform::DX11Renderer& renderer);

    const char* GetTitle() const override { return "Inicio"; }
    const char* GetIcon() const override { return "I"; }
    void OnRender(float deltaTimeSeconds) override;
    void OnTick(uint64_t tickMs) override;

private:
    // removed is set to true if the card's "Quitar" button deleted the
    // entry -- caller erases it from the working list.
    void RenderEntryCard(startup::StartupEntry& entry, bool& removed);

    HWND hwnd_;
    startup::StartupScanner scanner_; // internally thread-safe, see StartupScanner.h
    platform::IconTextureCache iconCache_; // only ever touched from OnRender (render thread)

    std::mutex scanMutex_; // guards lastScan_ between OnTick (background) and OnRender (main thread)
    startup::ScanResult lastScan_;
    std::atomic<bool> hasScannedOnce_{false};
    uint64_t tickCounter_ = 0;

    std::string lastErrorMessage_;
    ui::AddStartupEntryDialog addDialog_;
};

} // namespace ui
