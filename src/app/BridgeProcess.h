#pragma once
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace app {

// Owns the lifecycle of MkPCApp.SensorBridge.exe: spawns it, watches for it
// exiting/crashing via a blocking wait (zero idle CPU), and respawns it with a
// capped retry count to avoid a crash-loop burning CPU indefinitely.
//
// processInfo_ is written by the watcher thread and read/terminated by Stop()
// (called from the UI thread), so it's guarded by processMutex_. Stop() also
// signals stopEvent_ rather than relying solely on TerminateProcess to wake the
// watcher thread — this closes a race where the watcher could be about to spawn
// a *new* process at the exact moment Stop() decides there's nothing to
// terminate, which would otherwise hang shutdown forever and leak the process.
class BridgeProcess {
public:
    ~BridgeProcess();

    // Locates MkPCApp.SensorBridge.exe relative to this executable's own path
    // (sensor-bridge/MkPCApp.SensorBridge.exe) and starts the watcher thread.
    void Start();

    // Signals the watcher thread to stop and terminates the bridge process, if
    // running. Call before the app exits. Safe to call multiple times.
    void Stop();

    bool IsAvailable() const { return available_.load(); }

private:
    void WatcherLoop();
    bool SpawnBridge();

    std::wstring bridgeExePath_;

    std::mutex processMutex_;
    PROCESS_INFORMATION processInfo_{}; // guarded by processMutex_

    // Every spawned bridge process is assigned to this job object with
    // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE set. If MkPCApp.exe ever dies without
    // running its own cleanup — killed via Task Manager, a debugger force-stop,
    // a crash — Windows closes all of its handles, including this one, which
    // makes the OS itself terminate the bridge process. This guarantees the
    // bridge can never outlive the main app, with no dependence on our own
    // shutdown code actually getting to run.
    HANDLE jobObject_ = nullptr;

    HANDLE stopEvent_ = nullptr;
    std::thread watcherThread_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> available_{false};

    static constexpr int kMaxRetries = 5;
    static constexpr int kRetryWindowSeconds = 300; // 5 minutes
};

} // namespace app
