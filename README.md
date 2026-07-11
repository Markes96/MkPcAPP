# MkPCApp

A personal Windows control-center app. Iteration 1 ships a single section: a
real-time hardware monitor (CPU/GPU usage & temperature, RAM, VRAM, fan
speeds, network throughput, disk space, uptime) with 60-second rolling
graphs. Future iterations add more sections (via the left icon sidebar)
without touching the app shell.

Built for Windows: Dear ImGui + DirectX 11 for the UI, plus a small
self-contained .NET sensor bridge process (wrapping LibreHardwareMonitorLib)
for sensors the native Win32 APIs can't reach reliably (temperatures, fan
speeds, GPU load/VRAM).

See `docs/ARCHITECTURE.md` for the design and `docs/PROJECT_STATUS.md` for
the current state of the app, its requirements, and known limitations.

## Build (Windows only)

Prerequisites:
- Visual Studio 2022 (Desktop development with C++ workload) or MSVC Build Tools + Windows SDK
- CMake >= 3.21
- .NET SDK 8.0
- Git

```powershell
git submodule update --init --recursive
./scripts/build.ps1
```

Output is placed in `build/bin/`: `MkPCApp.exe` plus `sensor-bridge/MkPCApp.SensorBridge.exe`.
If you change build setups (move folders, switch CMake generators, bump the
LibreHardwareMonitorLib version), delete `build/bin` and rebuild from scratch
— an incremental build can leave the bridge `.exe` stale.

## Run

Run `build/bin/MkPCApp.exe`. It always requests administrator elevation (a
UAC prompt appears on every launch) — this is required for full sensor
coverage (CPU temperature, fan speeds). Closing the window (X) minimizes it
to the system tray instead of exiting; use the tray icon's context menu to
reopen or fully exit. Launching the app again while it's already running
(even minimized to tray) just brings the existing window to the foreground.
