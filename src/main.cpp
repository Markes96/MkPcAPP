#include "app/Application.h"
#include "platform/WndProc.h"
#include <windows.h>
#include <cwchar>

namespace {
constexpr wchar_t kWindowClassName[] = L"MkPCAppWindowClass";
constexpr wchar_t kWindowTitle[] = L"MkPCApp";
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmdLine, int) {
    bool startMinimized = cmdLine && wcsstr(cmdLine, L"--minimized") != nullptr;

    // Single-instance guard: a named mutex, checked before creating any
    // window. If it already exists, another instance owns it — find that
    // instance's window (unique class name) and bring it to the foreground
    // instead of starting a second, redundant instance (and, pre-fix, a
    // second bridge process racing the first one's shared memory).
    HANDLE singleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\MkPCApp_SingleInstance");
    if (singleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClassName, nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(singleInstanceMutex);
        return 0;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = platform::WndProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&windowClass);

    HWND hwnd = CreateWindowExW(0, kWindowClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                 CW_USEDEFAULT, CW_USEDEFAULT, 1100, 750, nullptr, nullptr, instance,
                                 nullptr);
    if (!hwnd) {
        return 1;
    }

    app::Application application;
    if (!application.Init(hwnd, instance)) {
        DestroyWindow(hwnd);
        return 1;
    }
    platform::g_application = &application;

    ShowWindow(hwnd, startMinimized ? SW_HIDE : SW_SHOW);
    if (!startMinimized) {
        UpdateWindow(hwnd);
    }

    // Zero-idle-CPU while hidden: a plain blocking GetMessage loop. Only while
    // the window is visible do we switch to a non-blocking PeekMessage loop so
    // we can render every frame (throttled by vsync inside RenderFrame/Present).
    MSG msg{};
    bool running = true;
    while (running) {
        if (IsWindowVisible(hwnd)) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (running && IsWindowVisible(hwnd)) {
                application.RenderFrame();
            }
        } else {
            BOOL result = GetMessageW(&msg, nullptr, 0, 0);
            if (result <= 0) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    platform::g_application = nullptr;
    application.Shutdown();
    DestroyWindow(hwnd);
    return 0;
}
