#pragma once
#include "SharedSensorData.h"
#include <windows.h>
#include <cstring>
#include <optional>

namespace ipc {

// Read-only view of the sensor bridge's shared memory segment, from the native
// (reader) side. Opens (does not create) the mapping so it works whether the
// bridge started before or after the native app first polls.
class SharedMemoryChannel {
public:
    SharedMemoryChannel() = default;
    ~SharedMemoryChannel() { Close(); }

    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;

    // Attempts to open the mapping if not already open. Safe to call every tick;
    // cheap no-op once successfully opened, and retries automatically if the
    // bridge hasn't created the mapping yet (or was restarted).
    bool EnsureOpen() {
        if (view_) {
            return true;
        }
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, kSharedMemoryName);
        if (!mapping) {
            return false;
        }
        void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SharedSensorData));
        if (!view) {
            CloseHandle(mapping);
            return false;
        }
        mapping_ = mapping;
        view_ = static_cast<const SharedSensorData*>(view);
        return true;
    }

    // Reads a torn-free snapshot using the seqlock protocol. Returns nullopt if
    // the mapping isn't open, the struct version is unrecognized, or a stable
    // read couldn't be obtained within a bounded number of retries (writer
    // pathologically busy, which should never happen at a 1 Hz write rate).
    std::optional<SharedSensorData> TryRead() {
        if (!EnsureOpen()) {
            return std::nullopt;
        }
        constexpr int kMaxAttempts = 8;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            uint64_t seqBefore = view_->writeSequence;
            if (seqBefore & 1) {
                // Writer is mid-update; yield and retry.
                Sleep(0);
                continue;
            }
            SharedSensorData snapshot;
            memcpy(&snapshot, view_, sizeof(SharedSensorData));
            if (view_->writeSequence != seqBefore) {
                continue; // torn read, retry
            }
            if (snapshot.structVersion != kStructVersion) {
                return std::nullopt; // stale/incompatible bridge build
            }
            return snapshot;
        }
        return std::nullopt;
    }

    void Close() {
        if (view_) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (mapping_) {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }
    }

private:
    HANDLE mapping_ = nullptr;
    const SharedSensorData* view_ = nullptr;
};

} // namespace ipc
