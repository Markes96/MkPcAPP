#include "Application.h"
#include "AutoStart.h"
#include "../ui/HardwareMonitorTab.h"
#include "../ui/PerfilesTab.h"
#include "../ui/StartupTab.h"
#include "../ui/Theme.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <implot.h>
#include <memory>

namespace app {

namespace {
// 1 Hz cadence for both native and bridge-derived metrics (see design doc,
// "Data Sources" table) — sensor reads never need to be faster than this.
constexpr LONGLONG kDataTickIntervalMs = 1000;
} // namespace

bool Application::Init(HWND hwnd, HINSTANCE /*instance*/) {
    hwnd_ = hwnd;

    if (!renderer_.Init(hwnd)) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ui::Theme::g_fonts = ui::Theme::LoadFonts();
    ui::Theme::Apply();
    ui::Theme::ApplyPlotStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(renderer_.Device(), renderer_.Context());

    tabManager_.RegisterTab(std::make_unique<ui::HardwareMonitorTab>(aggregator_));

    profileManager_.Init();
    automationEngine_.Init(profileManager_);
    profileManager_.ReconcileOnStartup(&automationEngine_);
    tabManager_.RegisterTab(std::make_unique<ui::PerfilesTab>(profileManager_, automationEngine_, hwnd_));
    tabManager_.RegisterTab(std::make_unique<ui::StartupTab>(hwnd_, renderer_));

    TrayIcon::Callbacks callbacks;
    callbacks.onOpen = [this]() { Show(); };
    callbacks.onToggleAutoStart = []() { AutoStart::SetEnabled(!AutoStart::IsEnabled()); };
    callbacks.isAutoStartEnabled = []() { return AutoStart::IsEnabled(); };
    callbacks.onExit = [this]() { RequestExit(); };
    trayIcon_.Create(hwnd, LoadIconW(nullptr, IDI_APPLICATION), callbacks);

    bridgeProcess_.Start();
    StartDataTickThread();

    lastFrameTimeMs_ = GetTickCount64();
    return true;
}

void Application::Shutdown() {
    stopDataTick_.store(true);
    if (dataTickThread_.joinable()) {
        dataTickThread_.join();
    }

    bridgeProcess_.Stop();
    trayIcon_.Destroy();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    renderer_.Shutdown();
}

void Application::RenderFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ULONGLONG now = GetTickCount64();
    float deltaSeconds = static_cast<float>(now - lastFrameTimeMs_) / 1000.0f;
    lastFrameTimeMs_ = now;

    tabManager_.Render(deltaSeconds);

    ImGui::Render();
    renderer_.BeginFrame();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    // Throttled FPS while visible: vsync via Present(1,...) is enough for a
    // numeric dashboard, no uncapped/high-refresh rendering needed.
    renderer_.EndFrameAndPresent(/*vsync=*/true);
}

void Application::OnResize(UINT width, UINT height) {
    renderer_.OnResize(width, height);
}

void Application::OnTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    trayIcon_.HandleMessage(hwnd, wParam, lParam);
}

void Application::OnPowerBroadcast(WPARAM /*wParam*/, LPARAM /*lParam*/) {
    // React immediately to a power-source change (AC<->battery) rather than
    // waiting up to 1s for the next DataTickLoop tick.
    automationEngine_.OnPowerSourceChangeHint();
}

void Application::Show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void Application::HideToTray() {
    ShowWindow(hwnd_, SW_HIDE);
}

void Application::RequestExit() {
    wantsExit_.store(true);
    PostMessage(hwnd_, WM_CLOSE, 0, 0);
}

void Application::StartDataTickThread() {
    dataTickThread_ = std::thread(&Application::DataTickLoop, this);
}

void Application::DataTickLoop() {
    HANDLE timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) {
        return;
    }

    LARGE_INTEGER dueTime;
    dueTime.QuadPart = 0; // fire immediately the first time
    SetWaitableTimer(timer, &dueTime, static_cast<LONG>(kDataTickIntervalMs), nullptr, nullptr, FALSE);

    while (!stopDataTick_.load()) {
        DWORD waitResult = WaitForSingleObject(timer, static_cast<DWORD>(kDataTickIntervalMs * 2));
        if (waitResult != WAIT_OBJECT_0) {
            continue;
        }
        if (stopDataTick_.load()) {
            break;
        }
        aggregator_.Tick();
        tabManager_.OnTick(GetTickCount64());
        automationEngine_.Tick();
    }

    CloseHandle(timer);
}

} // namespace app
