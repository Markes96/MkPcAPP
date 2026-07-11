#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include "NativeSensors.h"

#pragma comment(lib, "iphlpapi.lib")

namespace sensors {

namespace {

uint64_t FileTimeToUint64(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Picks the network interface currently carrying the most traffic — a simple,
// good-enough heuristic for "the primary adapter" without depending on routing
// table details. Re-resolved periodically (not every tick) since this walks the
// full interface table.
bool ResolvePrimaryInterface(NET_LUID& outLuid) {
    MIB_IF_TABLE2* table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) {
        return false;
    }

    bool found = false;
    ULONG64 bestTotal = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.InterfaceAndOperStatusFlags.HardwareInterface == FALSE) {
            continue; // skip virtual/software interfaces (loopback, tunnels, etc.)
        }
        if (row.OperStatus != IfOperStatusUp) {
            continue;
        }
        ULONG64 total = row.InOctets + row.OutOctets;
        if (total >= bestTotal) {
            bestTotal = total;
            outLuid = row.InterfaceLuid;
            found = true;
        }
    }

    FreeMibTable(table);
    return found;
}

bool ReadInterfaceCounters(const NET_LUID& luid, uint64_t& inOctets, uint64_t& outOctets) {
    MIB_IF_ROW2 row{};
    row.InterfaceLuid = luid;
    if (GetIfEntry2(&row) != NO_ERROR) {
        return false;
    }
    inOctets = row.InOctets;
    outOctets = row.OutOctets;
    return true;
}

} // namespace

NativeSensors::NativeSensors() = default;

NativeSnapshot NativeSensors::Poll() {
    NativeSnapshot out;
    RefreshCpuUsage(out);
    RefreshRam(out);
    RefreshNetwork(out);
    RefreshDisks(out);
    RefreshUptime(out);
    return out;
}

void NativeSensors::RefreshCpuUsage(NativeSnapshot& out) {
    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        out.cpuUsagePercent = 0.0f;
        return;
    }

    ULARGE_INTEGER idle, kernel, user;
    idle.QuadPart = FileTimeToUint64(idleFt);
    kernel.QuadPart = FileTimeToUint64(kernelFt);
    user.QuadPart = FileTimeToUint64(userFt);

    if (!haveCpuBaseline_) {
        prevIdleTime_ = idle;
        prevKernelTime_ = kernel;
        prevUserTime_ = user;
        haveCpuBaseline_ = true;
        out.cpuUsagePercent = 0.0f;
        return;
    }

    uint64_t idleDelta = idle.QuadPart - prevIdleTime_.QuadPart;
    uint64_t kernelDelta = kernel.QuadPart - prevKernelTime_.QuadPart;
    uint64_t userDelta = user.QuadPart - prevUserTime_.QuadPart;
    uint64_t totalDelta = kernelDelta + userDelta; // kernel time includes idle time on Windows

    prevIdleTime_ = idle;
    prevKernelTime_ = kernel;
    prevUserTime_ = user;

    if (totalDelta == 0) {
        out.cpuUsagePercent = 0.0f;
        return;
    }

    uint64_t busyDelta = totalDelta - idleDelta;
    out.cpuUsagePercent = static_cast<float>(busyDelta) * 100.0f / static_cast<float>(totalDelta);
}

void NativeSensors::RefreshRam(NativeSnapshot& out) {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return;
    }
    out.ramTotalBytes = status.ullTotalPhys;
    out.ramUsedBytes = status.ullTotalPhys - status.ullAvailPhys;
    out.ramUsagePercent = static_cast<float>(status.dwMemoryLoad);
}

void NativeSensors::RefreshNetwork(NativeSnapshot& out) {
    constexpr int kResolveIntervalTicks = 30; // re-resolve primary adapter every ~30s

    if (!havePrimaryInterface_ || ticksSinceInterfaceResolve_ >= kResolveIntervalTicks) {
        NET_LUID primaryInterfaceLuid{};
        havePrimaryInterface_ = ResolvePrimaryInterface(primaryInterfaceLuid);
        primaryInterfaceLuidValue_ = primaryInterfaceLuid.Value;
        ticksSinceInterfaceResolve_ = 0;
        haveNetBaseline_ = false; // discard stale counters from the old interface
    }
    ++ticksSinceInterfaceResolve_;

    if (!havePrimaryInterface_) {
        out.networkUpKBps = 0.0f;
        out.networkDownKBps = 0.0f;
        return;
    }

    uint64_t inOctets = 0, outOctets = 0;
    NET_LUID primaryInterfaceLuid{};
    primaryInterfaceLuid.Value = primaryInterfaceLuidValue_;
    if (!ReadInterfaceCounters(primaryInterfaceLuid, inOctets, outOctets)) {
        havePrimaryInterface_ = false; // adapter likely disappeared; re-resolve next tick
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (!haveNetBaseline_) {
        prevInOctets_ = inOctets;
        prevOutOctets_ = outOctets;
        prevNetSampleTimeMs_ = now;
        haveNetBaseline_ = true;
        return;
    }

    double elapsedSeconds = static_cast<double>(now - prevNetSampleTimeMs_) / 1000.0;
    if (elapsedSeconds <= 0.0) {
        return;
    }

    uint64_t inDelta = (inOctets >= prevInOctets_) ? (inOctets - prevInOctets_) : 0;
    uint64_t outDelta = (outOctets >= prevOutOctets_) ? (outOctets - prevOutOctets_) : 0;

    out.networkDownKBps = static_cast<float>(inDelta / 1024.0 / elapsedSeconds);
    out.networkUpKBps = static_cast<float>(outDelta / 1024.0 / elapsedSeconds);

    prevInOctets_ = inOctets;
    prevOutOctets_ = outOctets;
    prevNetSampleTimeMs_ = now;
}

void NativeSensors::RefreshDisks(NativeSnapshot& out) {
    constexpr int kDiskRefreshIntervalTicks = 8; // disk space changes slowly

    if (ticksSinceDiskRefresh_ < kDiskRefreshIntervalTicks) {
        ++ticksSinceDiskRefresh_;
        out.drives = cachedDrives_;
        return;
    }
    ticksSinceDiskRefresh_ = 0;

    std::vector<DriveSpace> drives;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) {
            continue;
        }
        wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        UINT type = GetDriveTypeW(root);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) {
            continue; // skip network/CD-ROM/unavailable drives to avoid slow/blocking calls
        }
        ULARGE_INTEGER freeBytes, totalBytes;
        if (!GetDiskFreeSpaceExW(root, nullptr, &totalBytes, &freeBytes)) {
            continue;
        }
        DriveSpace drive;
        drive.rootPath = root;
        drive.totalBytes = totalBytes.QuadPart;
        drive.freeBytes = freeBytes.QuadPart;
        drives.push_back(drive);
    }

    cachedDrives_ = drives;
    out.drives = drives;
}

void NativeSensors::RefreshUptime(NativeSnapshot& out) {
    out.uptimeMs = GetTickCount64();
}

} // namespace sensors
