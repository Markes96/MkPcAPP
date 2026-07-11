#pragma once
#include "NativeSensors.h"
#include "../ipc/SharedMemoryChannel.h"
#include "../ui/RingBuffer.h"
#include <windows.h>
#include <mutex>
#include <optional>

namespace sensors {

namespace detail {
// Current wall-clock time in the same units the bridge stamps its snapshots
// with (DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()) — needed to detect a
// bridge that died without anyone removing its last-written shared memory
// snapshot, which would otherwise read as "available" forever.
inline uint64_t CurrentUnixMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // 100ns intervals since 1601-01-01 -> ms since 1970-01-01.
    constexpr uint64_t kEpochDiff100ns = 116444736000000000ULL;
    return (uli.QuadPart - kEpochDiff100ns) / 10000ULL;
}
} // namespace detail

// One second's worth of every metric the Hardware Monitor tab plots or displays.
struct CombinedSample {
    NativeSnapshot native;
    std::optional<ipc::SharedSensorData> bridge; // nullopt if the bridge is unavailable or stale
};

// Merges NativeSensors (always available) with the shared-memory bridge snapshot
// (may be unavailable) and maintains the 60-sample rolling history for each
// plotted series. Polled once per second from a dedicated background thread;
// read from the render thread via GetLatest()/GetHistory(), both mutex-guarded
// since writes (1 Hz) and reads (render rate) happen on different threads.
class SensorAggregator {
public:
    // Call once per second from the data-tick thread.
    void Tick() {
        NativeSnapshot native = nativeSensors_.Poll();
        std::optional<ipc::SharedSensorData> bridge = bridgeChannel_.TryRead();

        // The bridge writes once per second; if the last snapshot is much older
        // than that, the bridge process most likely died without anyone freeing
        // the shared memory it last wrote — treat it as unavailable instead of
        // silently freezing the displayed values (this matters for a monitoring
        // app: a frozen "GPU Temp" reading could hide an actual thermal issue).
        constexpr uint64_t kStalenessThresholdMs = 3000;
        if (bridge.has_value()) {
            uint64_t nowMs = detail::CurrentUnixMs();
            if (nowMs > bridge->timestampUnixMs && nowMs - bridge->timestampUnixMs > kStalenessThresholdMs) {
                bridge.reset();
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        latest_.native = native;
        latest_.bridge = bridge;

        cpuUsageHistory_.Push(native.cpuUsagePercent);
        ramUsageHistory_.Push(native.ramUsagePercent);
        netUpHistory_.Push(native.networkUpKBps);
        netDownHistory_.Push(native.networkDownKBps);

        if (bridge.has_value() && (bridge->sensorsAvailable & ipc::kCpuTempAvailable)) {
            cpuTempHistory_.Push(bridge->cpuTempC);
        }
        if (bridge.has_value() && (bridge->sensorsAvailable & ipc::kGpuUsageAvailable)) {
            gpuUsageHistory_.Push(bridge->gpuUsagePercent);
        }
        if (bridge.has_value() && (bridge->sensorsAvailable & ipc::kGpuTempAvailable)) {
            gpuTempHistory_.Push(bridge->gpuTempC);
        }
        if (bridge.has_value() && (bridge->sensorsAvailable & ipc::kVramAvailable) &&
            bridge->vramTotalMB > 0.0f) {
            gpuVramHistory_.Push(100.0f * bridge->vramUsedMB / bridge->vramTotalMB);
        }
    }

    CombinedSample GetLatest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return latest_;
    }

    // Copies are cheap (60 floats) and avoid holding the lock while the render
    // thread iterates the buffer.
    ui::RingBuffer<float, 60> GetCpuUsageHistory() const { return CopyLocked(cpuUsageHistory_); }
    ui::RingBuffer<float, 60> GetRamUsageHistory() const { return CopyLocked(ramUsageHistory_); }
    ui::RingBuffer<float, 60> GetCpuTempHistory() const { return CopyLocked(cpuTempHistory_); }
    ui::RingBuffer<float, 60> GetGpuUsageHistory() const { return CopyLocked(gpuUsageHistory_); }
    ui::RingBuffer<float, 60> GetGpuTempHistory() const { return CopyLocked(gpuTempHistory_); }
    ui::RingBuffer<float, 60> GetGpuVramHistory() const { return CopyLocked(gpuVramHistory_); }
    ui::RingBuffer<float, 60> GetNetUpHistory() const { return CopyLocked(netUpHistory_); }
    ui::RingBuffer<float, 60> GetNetDownHistory() const { return CopyLocked(netDownHistory_); }

private:
    ui::RingBuffer<float, 60> CopyLocked(const ui::RingBuffer<float, 60>& buffer) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer;
    }

    NativeSensors nativeSensors_;
    ipc::SharedMemoryChannel bridgeChannel_;

    mutable std::mutex mutex_;
    CombinedSample latest_;
    ui::RingBuffer<float, 60> cpuUsageHistory_;
    ui::RingBuffer<float, 60> ramUsageHistory_;
    ui::RingBuffer<float, 60> cpuTempHistory_;
    ui::RingBuffer<float, 60> gpuUsageHistory_;
    ui::RingBuffer<float, 60> gpuTempHistory_;
    ui::RingBuffer<float, 60> gpuVramHistory_;
    ui::RingBuffer<float, 60> netUpHistory_;
    ui::RingBuffer<float, 60> netDownHistory_;
};

} // namespace sensors
