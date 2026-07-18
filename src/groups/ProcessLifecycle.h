#pragma once
#include <windows.h>
#include <string>

namespace groups {

// True if any running process' full image path matches resolvedExePath
// case-insensitively -- takes a fresh CreateToolhelp32Snapshot each call
// (TH32CS_SNAPPROCESS) plus one QueryFullProcessImageNameW per process,
// so only meant for a low-frequency, user-triggered check (the "Abrir
// grupo" click), never per-frame.
bool IsProcessRunning(const std::wstring& resolvedExePath);

struct LaunchResult {
    bool ok = false;
    DWORD pid = 0;
    // Valid only if ok is true. Caller (GroupProcessTracker) takes
    // ownership -- must CloseHandle once it's done tracking this process.
    HANDLE processHandle = nullptr;
};

// CreateProcess wrapper. Working directory is set to exePath's own folder
// (many GUI apps assume their own directory as CWD to find adjacent
// resources) -- unlike BridgeProcess's hidden, IDLE_PRIORITY sensor
// bridge, this must show its own window normally (it's launching
// user-facing apps like a game or Discord), so no CREATE_NO_WINDOW /
// priority-class flags.
LaunchResult LaunchProcess(const std::wstring& exePath, const std::wstring& args);

// Sends WM_CLOSE to every visible top-level window owned by `pid` -- does
// not wait for the process to actually exit. Never blocks (called from
// GroupProcessTracker::ReleaseOwnership on the render thread) -- the
// caller polls IsProcessAlive on a later tick and force-closes after a
// timeout instead.
void RequestGracefulClose(DWORD pid);

// Non-blocking liveness check (WaitForSingleObject with a zero timeout).
bool IsProcessAlive(HANDLE processHandle);

// Returns false only if TerminateProcess itself reports failure (e.g. no
// permission) -- a process that had already exited on its own still
// counts as success, nothing left to terminate.
bool ForceTerminate(HANDLE processHandle);

} // namespace groups
