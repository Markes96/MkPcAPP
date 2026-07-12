#pragma once
#include "TrayIcon.h"
#include "BridgeProcess.h"
#include "../platform/DX11Renderer.h"
#include "../profiles/AutomationEngine.h"
#include "../profiles/ProfileManager.h"
#include "../sensors/SensorAggregator.h"
#include "../ui/TabManager.h"
#include <windows.h>
#include <atomic>
#include <thread>

namespace app {

// Owns everything the app needs for its lifetime: the window's DX11 renderer,
// the tab system, the tray icon, the sensor bridge process, and the background
// data-tick thread. main.cpp only drives the Win32 message loop and forwards
// events here.
class Application {
public:
    bool Init(HWND hwnd, HINSTANCE instance);
    void Shutdown();

    // Renders one frame; call only while the window is visible.
    void RenderFrame();

    void OnResize(UINT width, UINT height);
    void OnTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnPowerBroadcast(WPARAM wParam, LPARAM lParam);

    void Show();
    void HideToTray();
    void RequestExit();

    bool WantsExit() const { return wantsExit_.load(); }

private:
    void StartDataTickThread();
    void DataTickLoop();

    HWND hwnd_ = nullptr;
    platform::DX11Renderer renderer_;
    sensors::SensorAggregator aggregator_;
    profiles::ProfileManager profileManager_;
    profiles::AutomationEngine automationEngine_;
    ui::TabManager tabManager_;
    TrayIcon trayIcon_;
    BridgeProcess bridgeProcess_;

    std::thread dataTickThread_;
    std::atomic<bool> stopDataTick_{false};
    std::atomic<bool> wantsExit_{false};

    ULONGLONG lastFrameTimeMs_ = 0;
};

} // namespace app
