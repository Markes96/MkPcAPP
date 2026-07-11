#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace sensors {

struct DriveSpace {
    std::wstring rootPath; // e.g. L"C:\\"
    uint64_t totalBytes = 0;
    uint64_t freeBytes = 0;
};

struct NativeSnapshot {
    float cpuUsagePercent = 0.0f;
    float ramUsagePercent = 0.0f;
    uint64_t ramUsedBytes = 0;
    uint64_t ramTotalBytes = 0;
    float networkUpKBps = 0.0f;
    float networkDownKBps = 0.0f;
    uint64_t uptimeMs = 0;
    std::vector<DriveSpace> drives; // refreshed at a slower cadence, see NativeSensors
};

// Reads CPU/RAM/network/disk/uptime via plain Win32 APIs — no elevation, no
// external dependency. Deliberately does not overlap with anything the .NET
// sensor bridge provides (temps/GPU/fans).
class NativeSensors {
public:
    NativeSensors();

    // Call once per second from the background data-tick thread. Internally
    // decides whether it's time to refresh the (slow-changing) disk list.
    NativeSnapshot Poll();

private:
    void RefreshCpuUsage(NativeSnapshot& out);
    void RefreshRam(NativeSnapshot& out);
    void RefreshNetwork(NativeSnapshot& out);
    void RefreshDisks(NativeSnapshot& out);
    void RefreshUptime(NativeSnapshot& out);

    // CPU% needs a delta between two GetSystemTimes samples.
    ULARGE_INTEGER prevIdleTime_{};
    ULARGE_INTEGER prevKernelTime_{};
    ULARGE_INTEGER prevUserTime_{};
    bool haveCpuBaseline_ = false;

    // Network throughput needs a delta between two interface-counter samples.
    uint64_t prevInOctets_ = 0;
    uint64_t prevOutOctets_ = 0;
    ULONGLONG prevNetSampleTimeMs_ = 0;
    bool haveNetBaseline_ = false;
    uint64_t primaryInterfaceLuidValue_ = 0;
    bool havePrimaryInterface_ = false;
    int ticksSinceInterfaceResolve_ = 0;

    // Disk space changes slowly; poll it far less often than every tick.
    std::vector<DriveSpace> cachedDrives_;
    int ticksSinceDiskRefresh_ = 1000; // force an immediate refresh on first Poll()
};

} // namespace sensors
