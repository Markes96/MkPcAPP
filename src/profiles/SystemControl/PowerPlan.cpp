#include "PowerPlan.h"
#include <windows.h>
#include <powrprof.h>
#include <combaseapi.h>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace profiles {
namespace PowerPlanControl {

namespace {

// Well-known Windows power scheme GUIDs (stable, documented — same values as
// `powercfg /list` on any Windows install).
constexpr GUID kBalancedGuid = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
constexpr GUID kHighPerformanceGuid = {
    0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
constexpr GUID kPowerSaverGuid = {0xa1841308, 0x3541, 0x4fab, {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};
// This is also the "Ultimate Performance" duplicate-scheme template GUID
// passed to PowerDuplicateScheme below — Windows hides this scheme by
// default and it must be duplicated once before it shows up in the scheme
// list (same GUID `powercfg -duplicatescheme` uses).
constexpr GUID kUltimatePerformanceGuid = {
    0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

// Every time Ultimate Performance is duplicated (Windows hides the original
// and PowerDuplicateScheme always returns a fresh random GUID for the copy),
// the new scheme is tagged with this friendly name AND its GUID is persisted
// to the registry below -- two independent, redundant ways to find it again
// later, so a single failure (a transient friendly-name write failure, or a
// lost/corrupted registry value) doesn't force duplicating yet another
// scheme and leaving the previous one an orphan forever.
constexpr wchar_t kOwnedUltimateName[] = L"MkPCApp Ultimate Performance";
constexpr wchar_t kRegistryKeyPath[] = L"Software\\MkPCApp\\PowerPlan";
constexpr wchar_t kRegistryValueName[] = L"UltimateSchemeGuid";

std::mutex& UltimatePerformanceMutex() {
    // Function-local static: thread-safe one-time init (C++11 "magic
    // statics"), no separate global-init-order concerns. ApplyUltimatePerformance
    // is reachable concurrently from the UI thread (manual apply) and the
    // background data-tick thread (automation) -- see ProfileManager::ApplyProfile,
    // which deliberately releases its own mutex before this call. Without this
    // lock, two concurrent callers could each conclude "no reusable scheme
    // exists yet" and each duplicate their own, leaving one an orphan.
    static std::mutex m;
    return m;
}

std::optional<std::wstring> ReadFriendlyName(const GUID& schemeGuid) {
    DWORD sizeBytes = 0;
    if (PowerReadFriendlyName(nullptr, &schemeGuid, nullptr, nullptr, nullptr, &sizeBytes) != ERROR_SUCCESS ||
        sizeBytes == 0) {
        return std::nullopt;
    }
    std::vector<UCHAR> buffer(sizeBytes);
    if (PowerReadFriendlyName(nullptr, &schemeGuid, nullptr, nullptr, buffer.data(), &sizeBytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(reinterpret_cast<const wchar_t*>(buffer.data()));
}

bool WriteFriendlyName(const GUID& schemeGuid, const wchar_t* name) {
    DWORD sizeBytes = static_cast<DWORD>((std::wcslen(name) + 1) * sizeof(wchar_t));
    return PowerWriteFriendlyName(nullptr, &schemeGuid, nullptr, nullptr,
                                    reinterpret_cast<UCHAR*>(const_cast<wchar_t*>(name)),
                                    sizeBytes) == ERROR_SUCCESS;
}

// Reads the GUID persisted by PersistUltimateGuid() below, if any. This is
// the primary, fast (O(1), no enumeration) way to find a previously
// duplicated scheme; ReadFriendlyName-based enumeration further below is
// only a slower fallback for when this registry value is missing/stale.
std::optional<GUID> ReadPersistedUltimateGuid() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    wchar_t buffer[64];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LSTATUS status =
        RegQueryValueExW(key, kRegistryValueName, nullptr, &type, reinterpret_cast<BYTE*>(buffer), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return std::nullopt;
    }

    GUID guid;
    if (FAILED(CLSIDFromString(buffer, &guid))) {
        return std::nullopt;
    }
    return guid;
}

// Best-effort: failing to persist just means the next apply falls back to
// the slower friendly-name enumeration (or, if that also fails, duplicates
// a fresh scheme) -- never fatal to the apply itself.
void PersistUltimateGuid(const GUID& guid) {
    wchar_t buffer[64];
    if (StringFromGUID2(guid, buffer, static_cast<int>(ARRAYSIZE(buffer))) == 0) {
        return;
    }

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                         nullptr) != ERROR_SUCCESS) {
        return;
    }
    RegSetValueExW(key, kRegistryValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(buffer),
                   static_cast<DWORD>((std::wcslen(buffer) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// Shared PowerEnumerate scanning loop: walks every ACCESS_SCHEME entry,
// stopping at the first one `predicate` accepts, or at the first
// non-success status (ERROR_NO_MORE_ITEMS or any other failure -- both just
// mean "stop, nothing (more) to find").
template <typename Predicate>
std::optional<GUID> EnumerateSchemesUntil(Predicate predicate) {
    ULONG index = 0;
    while (true) {
        GUID candidate;
        DWORD bufferSize = sizeof(candidate);
        DWORD status = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                                       reinterpret_cast<UCHAR*>(&candidate), &bufferSize);
        if (status != ERROR_SUCCESS) {
            return std::nullopt;
        }
        if (predicate(candidate)) {
            return candidate;
        }
        ++index;
    }
}

bool SchemeIsEnumerable(const GUID& schemeGuid) {
    return EnumerateSchemesUntil([&](const GUID& candidate) { return IsEqualGUID(candidate, schemeGuid); })
        .has_value();
}

// Slower fallback for finding a previously-duplicated scheme when the
// persisted-GUID registry value is missing/stale: scans every scheme
// looking for one already tagged kOwnedUltimateName.
std::optional<GUID> FindOwnedUltimateScheme() {
    return EnumerateSchemesUntil([](const GUID& candidate) {
        std::optional<std::wstring> name = ReadFriendlyName(candidate);
        return name.has_value() && *name == kOwnedUltimateName;
    });
}

ApplyResult ApplySchemeGuid(const GUID& guid) {
    return PowerSetActiveScheme(nullptr, &guid) == ERROR_SUCCESS ? ApplyResult::Ok : ApplyResult::Failed;
}

ApplyResult ApplyUltimatePerformance() {
    std::lock_guard<std::mutex> lock(UltimatePerformanceMutex());

    if (SchemeIsEnumerable(kUltimatePerformanceGuid)) {
        return ApplySchemeGuid(kUltimatePerformanceGuid);
    }

    // Fast path: a GUID persisted from a previous run's duplicate. Avoids
    // the O(number of schemes) enumeration below in the common case, and
    // means a single missing/failed friendly-name tag doesn't force
    // duplicating a fresh scheme every time.
    if (std::optional<GUID> persisted = ReadPersistedUltimateGuid();
        persisted.has_value() && SchemeIsEnumerable(*persisted) && ApplySchemeGuid(*persisted) == ApplyResult::Ok) {
        return ApplyResult::Ok;
    }

    // Slower fallback: the registry value may be missing/stale, but a
    // previous duplicate might still carry our friendly-name tag.
    if (std::optional<GUID> owned = FindOwnedUltimateScheme(); owned.has_value()) {
        PersistUltimateGuid(*owned); // heal the registry value for next time
        if (ApplySchemeGuid(*owned) == ApplyResult::Ok) {
            return ApplyResult::Ok;
        }
    }

    GUID* duplicatedGuid = nullptr;
    DWORD dupStatus = PowerDuplicateScheme(nullptr, &kUltimatePerformanceGuid, &duplicatedGuid);
    if (dupStatus == ERROR_SUCCESS && duplicatedGuid != nullptr) {
        GUID guidCopy = *duplicatedGuid;
        LocalFree(duplicatedGuid);
        WriteFriendlyName(guidCopy, kOwnedUltimateName); // best-effort, redundant with the registry value below
        PersistUltimateGuid(guidCopy); // primary dedupe mechanism for future applies
        if (ApplySchemeGuid(guidCopy) == ApplyResult::Ok) {
            return ApplyResult::Ok;
        }
    }

    // Best-effort fallback: Ultimate Performance couldn't be enabled at all
    // -- neither natively, nor by reusing a previous duplicate, nor by
    // duplicating a fresh one -- so apply High Performance instead. Reached
    // uniformly on every failure above, not just a fresh duplicate's
    // failure, so a broken reused scheme doesn't fail the whole apply.
    return ApplySchemeGuid(kHighPerformanceGuid);
}

} // namespace

ApplyResult SetActiveScheme(PowerPlan plan) {
    switch (plan) {
        case PowerPlan::Saver:
            return ApplySchemeGuid(kPowerSaverGuid);
        case PowerPlan::Balanced:
            return ApplySchemeGuid(kBalancedGuid);
        case PowerPlan::HighPerformance:
            return ApplySchemeGuid(kHighPerformanceGuid);
        case PowerPlan::UltimatePerformance:
            return ApplyUltimatePerformance();
    }
    return ApplyResult::Failed;
}

std::optional<PowerPlan> GetActiveScheme() {
    GUID* activeGuid = nullptr;
    if (PowerGetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS || activeGuid == nullptr) {
        return std::nullopt;
    }
    GUID guid = *activeGuid;
    LocalFree(activeGuid);

    if (IsEqualGUID(guid, kPowerSaverGuid)) return PowerPlan::Saver;
    if (IsEqualGUID(guid, kBalancedGuid)) return PowerPlan::Balanced;
    if (IsEqualGUID(guid, kHighPerformanceGuid)) return PowerPlan::HighPerformance;
    // A duplicated Ultimate Performance scheme gets a fresh random GUID, not
    // this template GUID -- recognize our own duplicate via the persisted
    // registry GUID first (fast), then the friendly name tag (fallback).
    if (IsEqualGUID(guid, kUltimatePerformanceGuid)) return PowerPlan::UltimatePerformance;

    if (std::optional<GUID> persisted = ReadPersistedUltimateGuid();
        persisted.has_value() && IsEqualGUID(guid, *persisted)) {
        return PowerPlan::UltimatePerformance;
    }

    std::optional<std::wstring> activeName = ReadFriendlyName(guid);
    if (activeName.has_value() && *activeName == kOwnedUltimateName) {
        return PowerPlan::UltimatePerformance;
    }

    return std::nullopt;
}

} // namespace PowerPlanControl
} // namespace profiles
