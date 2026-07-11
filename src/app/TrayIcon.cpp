#include "TrayIcon.h"
#include <shellapi.h>
#include <string>
#include <utility>

#pragma comment(lib, "shell32.lib")

namespace app {

namespace {
constexpr UINT kMenuIdOpen = 1;
constexpr UINT kMenuIdAutoStart = 2;
constexpr UINT kMenuIdExit = 3;
} // namespace

void TrayIcon::Create(HWND hwnd, HICON icon, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);

    iconData_ = {};
    iconData_.cbSize = sizeof(NOTIFYICONDATAW);
    iconData_.hWnd = hwnd;
    iconData_.uID = kTrayIconId;
    iconData_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    iconData_.uCallbackMessage = WM_APP_TRAYICON;
    iconData_.hIcon = icon;
    wcsncpy_s(iconData_.szTip, L"MkPCApp", _TRUNCATE);

    created_ = Shell_NotifyIconW(NIM_ADD, &iconData_) != FALSE;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &iconData_);
        created_ = false;
    }
}

void TrayIcon::SetTooltip(const wchar_t* text) {
    if (!created_) {
        return;
    }
    wcsncpy_s(iconData_.szTip, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &iconData_);
}

void TrayIcon::HandleMessage(HWND hwnd, WPARAM /*wParam*/, LPARAM lParam) {
    switch (lParam) {
        case WM_LBUTTONDBLCLK:
            if (callbacks_.onOpen) {
                callbacks_.onOpen();
            }
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            break;
        default:
            break;
    }
}

void TrayIcon::ShowContextMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuIdOpen, L"Open");

    bool autoStartEnabled = callbacks_.isAutoStartEnabled ? callbacks_.isAutoStartEnabled() : false;
    AppendMenuW(menu, MF_STRING | (autoStartEnabled ? MF_CHECKED : 0), kMenuIdAutoStart,
                L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuIdExit, L"Exit");

    POINT cursor;
    GetCursorPos(&cursor);

    // Documented Win32 quirk: the window must be the foreground window for the
    // menu to dismiss correctly when the user clicks elsewhere.
    SetForegroundWindow(hwnd);
    UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0, hwnd,
                                    nullptr);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    switch (selected) {
        case kMenuIdOpen:
            if (callbacks_.onOpen) callbacks_.onOpen();
            break;
        case kMenuIdAutoStart:
            if (callbacks_.onToggleAutoStart) callbacks_.onToggleAutoStart();
            break;
        case kMenuIdExit:
            if (callbacks_.onExit) callbacks_.onExit();
            break;
        default:
            break;
    }
}

} // namespace app
