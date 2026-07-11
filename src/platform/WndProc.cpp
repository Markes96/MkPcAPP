#include "WndProc.h"
#include "../app/Application.h"
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace platform {

app::Application* g_application = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
        case WM_SIZE:
            if (g_application && wParam != SIZE_MINIMIZED) {
                g_application->OnResize(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case app::WM_APP_TRAYICON:
            if (g_application) {
                g_application->OnTrayMessage(hwnd, wParam, lParam);
            }
            return 0;

        case WM_CLOSE:
            // Minimize to tray instead of exiting, unless RequestExit() posted
            // this WM_CLOSE itself (tray "Exit" menu item).
            if (g_application && g_application->WantsExit()) {
                DestroyWindow(hwnd);
            } else if (g_application) {
                g_application->HideToTray();
            } else {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace platform
