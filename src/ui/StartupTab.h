#pragma once
#include "ITab.h"
#include "AddStartupEntryDialog.h"
#include "ConfirmDeleteDialog.h"
#include "../startup/StartupScanner.h"
#include "../startup/AppInfoReader.h"
#include "../platform/IconTextureCache.h"
#include "../platform/DX11Renderer.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ui {

// The "Inicio" section: lists third-party apps registered to launch with
// Windows (Registry Run under HKCU/HKLM, plus both Startup-folder shortcut
// locations) as a horizontal list, one row per entry. Lets the user
// enable/disable them (reversible), see extra info (path/signer/size/
// version) via a popup, open the executable's containing folder, add a new
// one manually, and delete ANY entry (registry value or shortcut -- never
// the real app, always behind a confirmation dialog). Apps signed by
// Microsoft are excluded entirely by startup::StartupScanner before this
// tab ever sees them.
class StartupTab : public ITab {
public:
    StartupTab(HWND hwnd, platform::DX11Renderer& renderer);

    const char* GetTitle() const override { return "Inicio"; }
    const char* GetIcon() const override { return "I"; }
    void OnRender(float deltaTimeSeconds) override;
    void OnTick(uint64_t tickMs) override;

private:
    struct EntryInfoCache {
        startup::AppInfo appInfo;
        startup::SignatureInfo signatureInfo;
    };

    void RenderEntryRow(startup::StartupEntry& entry);
    void RenderInfoPopup(const startup::StartupEntry& entry);
    void RenderInfoContent(const startup::StartupEntry& entry);
    void OpenContainingFolder(const startup::StartupEntry& entry);

    HWND hwnd_;
    startup::StartupScanner scanner_; // internally thread-safe, see StartupScanner.h
    platform::IconTextureCache iconCache_; // only ever touched from OnRender (render thread)

    std::mutex scanMutex_; // guards lastScan_ between OnTick (background) and OnRender (main thread)
    startup::ScanResult lastScan_;
    std::atomic<bool> hasScannedOnce_{false};
    uint64_t tickCounter_ = 0;

    std::string lastErrorMessage_;
    ui::AddStartupEntryDialog addDialog_;
    ui::ConfirmDeleteDialog confirmDeleteDialog_;

    // Info popup state: which entry's popup is open (empty = none), and a
    // cache of already-read app info/signature data keyed by resolved exe
    // path -- computed once when the "i" button is clicked, not every
    // frame, since VERSIONINFO/Authenticode reads are real file I/O.
    std::string infoPopupEntryId_;
    std::unordered_map<std::string, EntryInfoCache> infoCache_;
};

} // namespace ui
