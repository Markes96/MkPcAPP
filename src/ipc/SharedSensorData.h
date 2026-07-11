#pragma once
#include <cstdint>

// Shared-memory contract between MkPCApp.exe (native reader) and
// MkPCApp.SensorBridge.exe (.NET writer, wraps LibreHardwareMonitorLib).
//
// MUST mirror sensor-bridge/MkPCApp.SensorBridge/SharedSensorData.cs byte-for-byte.
// Bump kStructVersion on ANY layout change in either file; the reader refuses to
// trust the struct's contents if the version doesn't match what it expects.

namespace ipc {

constexpr uint32_t kStructVersion = 1;
constexpr const wchar_t* kSharedMemoryName = L"Local\\MkPCApp_SensorData_v1";
constexpr const wchar_t* kReadyEventName = L"Local\\MkPCApp_SensorData_Ready_v1";
constexpr int kMaxFans = 8;

enum SensorAvailableBits : uint8_t {
    kCpuTempAvailable = 1 << 0,
    kGpuUsageAvailable = 1 << 1,
    kGpuTempAvailable = 1 << 2,
    kVramAvailable = 1 << 3,
    kFansAvailable = 1 << 4,
};

#pragma pack(push, 1)
struct SharedSensorData {
    uint32_t structVersion;   // must equal kStructVersion
    uint64_t writeSequence;   // seqlock: odd = write in progress, even = stable
    uint64_t timestampUnixMs;

    float cpuTempC;
    float gpuUsagePercent;
    float gpuTempC;
    float vramUsedMB;
    float vramTotalMB;

    uint32_t fanCount;
    float fanRpm[kMaxFans];
    char fanLabel[kMaxFans][32];

    char gpuName[64];
    char cpuName[64];

    uint8_t sensorsAvailable; // bitmask, see SensorAvailableBits
    uint8_t bridgeElevated;   // 1 if the bridge process is running elevated
    uint8_t reserved[2];
};
#pragma pack(pop)

static_assert(sizeof(SharedSensorData) <= 4096, "SharedSensorData must fit in one page");

} // namespace ipc
