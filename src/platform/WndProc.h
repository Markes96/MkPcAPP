#pragma once
#include <windows.h>

namespace app { class Application; }

namespace platform {

// Set once from main.cpp before the message loop starts; WndProc forwards
// window/tray events to it. There is exactly one Application per process.
extern app::Application* g_application;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

} // namespace platform
