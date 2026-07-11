#pragma once
#include <windows.h>
#include <functional>

namespace app {

constexpr UINT WM_APP_TRAYICON = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

// Shell_NotifyIcon wrapper: icon + tooltip + right-click context menu (Open,
// Start with Windows, Exit). All UI-thread only (must be constructed/used from
// the thread that owns the window).
class TrayIcon {
public:
    struct Callbacks {
        std::function<void()> onOpen;
        std::function<void()> onToggleAutoStart;
        std::function<bool()> isAutoStartEnabled;
        std::function<void()> onExit;
    };

    void Create(HWND hwnd, HICON icon, Callbacks callbacks);
    void Destroy();

    // Throttled by the caller (e.g. every 5s) to avoid unnecessary shell IPC.
    void SetTooltip(const wchar_t* text);

    // Call from the window proc when it receives WM_APP_TRAYICON.
    void HandleMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);

private:
    void ShowContextMenu(HWND hwnd);

    NOTIFYICONDATAW iconData_{};
    Callbacks callbacks_;
    bool created_ = false;
};

} // namespace app
