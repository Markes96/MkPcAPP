# Architecture

Two processes, one shared-memory channel:

```
MkPCApp.exe (C++, native, DX11 + Dear ImGui + tray, always elevated)
        |  memory-mapped file "Local\MkPCApp_SensorData_v1"
        |  (POD struct, seqlock-style versioned write, 1 Hz)
        v
MkPCApp.SensorBridge.exe (.NET 8, self-contained, no UI, wraps LibreHardwareMonitorLib)
```

## Why two processes

LibreHardwareMonitorLib is .NET-only. Rather than hosting the CLR in-process via
C++/CLI (which forces `/clr` compilation, drags in CLR load cost at startup, and
means a .NET crash could take down the whole app), the sensor reader runs as a
separate self-contained process. If it fails to start or crashes, the main
window still opens with native metrics (CPU/RAM/network/disk/uptime) intact.

## Why shared memory instead of pipes/JSON

The data is "latest value wins" — no queuing needed, sensor cadence is 1 Hz. A
fixed-size POD struct read via `memcpy` (`src/ipc/SharedSensorData.h`, mirrored
in `sensor-bridge/MkPCApp.SensorBridge/SharedSensorData.cs`) has zero
parsing/allocation cost. A seqlock (`writeSequence`: odd = write in progress,
even = stable) avoids needing a cross-process mutex for a once-per-second read.
The bridge creates the mapping with `MemoryMappedFile.CreateOrOpen` (not
`CreateNew`) since the native side keeps its own read handle open for its
whole session, which keeps the kernel object alive across bridge restarts —
`CreateNew` would throw "already exists" in that case.

## Process ownership

`src/app/BridgeProcess.cpp` spawns the bridge via `CreateProcess`
(`IDLE_PRIORITY_CLASS`), watches liveness via `WaitForMultipleObjects` on the
process handle and a manual-reset stop event (so `Stop()` can always wake the
watcher regardless of exactly when it's called, not just by racing
`TerminateProcess` against a handle that might already be gone), and respawns
on crash with a capped retry (5 attempts / 5 minutes) to avoid a crash-loop
burning CPU. Every spawned bridge process is assigned to a Windows Job Object
with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, so if `MkPCApp.exe` ever dies
without running its own cleanup (crash, Task Manager, a debugger force-stop),
Windows itself terminates the bridge instead of leaving it orphaned.

## No metric overlap

CPU%, RAM, network, disk, and uptime are read natively
(`src/sensors/NativeSensors.cpp`). CPU/GPU temperature, GPU load/VRAM, and fan
speeds come only from the bridge (`sensor-bridge/MkPCApp.SensorBridge/LhmSensorReader.cs`).
See `docs/PROJECT_STATUS.md` for the current state of what's implemented and
any known sensor limitations.

## Elevation

`MkPCApp.exe` always requests administrator elevation via a
`requireAdministrator` manifest (`CMakeLists.txt`, MSVC `/MANIFESTUAC` linker
flag) — CPU temperature and fan speeds are only accessible elevated on most
hardware. The bridge process, spawned as a child, inherits this elevation.
LibreHardwareMonitorLib is deliberately pinned to 0.9.4 rather than the latest
stable release — see `docs/PROJECT_STATUS.md` for why (newer versions need a
separate driver package this app doesn't install).

## Single instance

A named mutex (`Local\MkPCApp_SingleInstance`), checked in `src/main.cpp`
before any window is created. If it already exists, the running instance's
window is located by its class name and brought to the foreground instead of
starting a redundant second instance (and a second bridge process).

## Efficiency (the top priority)

- **Zero render loop while hidden in tray**: `src/main.cpp`'s message loop uses
  a blocking `GetMessage` when the window is hidden, and only switches to a
  non-blocking `PeekMessage` + render loop while visible.
- **Data polling decoupled from rendering**: `Application::DataTickLoop`
  (`src/app/Application.cpp`) runs on its own thread via `CreateWaitableTimer`,
  independent of the render thread.
- **Vsync-throttled rendering** while visible (`DX11Renderer::EndFrameAndPresent`)
  — no need for high frame rates on a numeric dashboard.
- **Bridge runs at `IDLE_PRIORITY_CLASS`**, sleeps between 1 Hz ticks.

## UI shell and extensibility

Navigation is a left icon sidebar (`ui::TabManager`, styled like an IDE
"Activity Bar"), not a top tab bar. Future sections implement `ui::ITab`
(`src/ui/ITab.h`) and register once with `ui::TabManager::RegisterTab` in
`Application::Init` — no other app-shell code changes. Visual styling (dark
VS Code-inspired palette, fonts, ImPlot colors) lives in `src/ui/Theme.h/.cpp`
and applies globally, so new sections get it for free.

## Perfiles module (`src/profiles/`)

The "Perfiles" tab (`ui::PerfilesTab`) is the second section built on the
`ITab` extension point above. It never touches Win32/WMI/COM directly —
`profiles::ProfileManager` owns the 5 fixed predefined profiles plus loaded
custom ones and the "which profile is active" state, `profiles::AutomationEngine`
owns the ordered, priority-evaluated rule list (time window / battery
threshold / power-source change), and both delegate all actual system
changes to narrow `profiles::SystemControl::*` wrappers (one per variable:
power plan, timeouts, brightness, volume, screen-off). Each
wrapper returns `ApplyResult{Ok, Unsupported, Failed}`
instead of a bare bool, so a profile apply can report exactly which variable
didn't take effect without blocking the rest — the same "degrade visibly,
never crash" principle the sensor code already follows for missing fan
sensors. Custom profiles and automation rules persist to
`%LOCALAPPDATA%\MkPCApp\profiles.json` via a small schema-specific
JSON reader/writer (`src/profiles/ProfileJson.*`) rather than a vendored
library — the file's shape is fixed and shallow enough that a general JSON
parser wasn't worth the dependency. `AutomationEngine::Tick()` reuses the
existing 1 Hz data-tick thread rather than spawning a second one, and also
reacts immediately to `WM_POWERBROADCAST` for power-source-change rules.
Night Light and Focus Assist were both deliberately left out: neither has a
public Win32 API, and both would have depended on writing an undocumented,
per-Windows-build registry blob format — see `docs/PROJECT_STATUS.md`.
