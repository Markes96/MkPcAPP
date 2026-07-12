#include "PowerTimeouts.h"
#include <windows.h>
#include <powrprof.h>

namespace profiles {
namespace PowerTimeouts {

namespace {

// Well-known subgroup/setting GUIDs for screen/sleep timeouts (stable,
// documented — same values `powercfg /aliases` shows as SUB_VIDEO/VIDEOIDLE
// and SUB_SLEEP/STANDBYIDLE). Defined locally rather than relying on the SDK
// header's extern declarations of the same-named constants, which only get
// storage if INITGUID is defined before including <powrprof.h> — a common
// "unresolved external symbol" trap otherwise.
//
// kStandbyTimeout confirmed against a real `powercfg /q` dump (STANDBYIDLE
// under SUB_SLEEP) -- the value previously here (...9fcb-8b30a1cd1b8b) was
// wrong in its last two GUID groups, a memorized-not-verified value that
// made every SetSleepTimeouts() call fail (PowerWrite*ValueIndex targeting a
// setting GUID unregistered under SUB_SLEEP), while SetScreenOffTimeouts()
// worked fine since kVideoSubgroup/kVideoPowerdownTimeout were correct.
constexpr GUID kVideoSubgroup = {0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
constexpr GUID kVideoPowerdownTimeout = {
    0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}};
constexpr GUID kSleepSubgroup = {0x238c9fa8, 0x0aad, 0x41ed, {0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20}};
constexpr GUID kStandbyTimeout = {0x29f6c1db, 0x86da, 0x48c5, {0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda}};
// HIBERNATEIDLE under SUB_SLEEP -- confirmed against a real `powercfg /q`
// dump, see docs/POWER_GUIDS.md.
constexpr GUID kHibernateTimeout = {0x9d7815a6, 0x7ee4, 0x497e, {0x88, 0x88, 0x51, 0x5a, 0x05, 0xf0, 0x23, 0x64}};

bool GetActiveSchemeGuid(GUID& outGuid) {
    GUID* activeGuid = nullptr;
    if (PowerGetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS || activeGuid == nullptr) {
        return false;
    }
    outGuid = *activeGuid;
    LocalFree(activeGuid);
    return true;
}

// Windows requires re-applying the active scheme after writing values to it
// for those values to actually take effect immediately.
ApplyResult WriteTimeouts(const GUID& subgroup, const GUID& setting, int acSeconds, int dcSeconds) {
    GUID activeGuid;
    if (!GetActiveSchemeGuid(activeGuid)) {
        return ApplyResult::Failed;
    }

    DWORD acStatus = PowerWriteACValueIndex(nullptr, &activeGuid, &subgroup, &setting,
                                             static_cast<DWORD>(acSeconds));
    DWORD dcStatus = PowerWriteDCValueIndex(nullptr, &activeGuid, &subgroup, &setting,
                                             static_cast<DWORD>(dcSeconds));
    if (acStatus != ERROR_SUCCESS || dcStatus != ERROR_SUCCESS) {
        return ApplyResult::Failed;
    }

    if (PowerSetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS) {
        return ApplyResult::Failed;
    }
    return ApplyResult::Ok;
}

std::optional<int> ReadAcTimeout(const GUID& subgroup, const GUID& setting) {
    GUID activeGuid;
    if (!GetActiveSchemeGuid(activeGuid)) {
        return std::nullopt;
    }
    DWORD value = 0;
    if (PowerReadACValueIndex(nullptr, &activeGuid, &subgroup, &setting, &value) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<int> ReadDcTimeout(const GUID& subgroup, const GUID& setting) {
    GUID activeGuid;
    if (!GetActiveSchemeGuid(activeGuid)) {
        return std::nullopt;
    }
    DWORD value = 0;
    if (PowerReadDCValueIndex(nullptr, &activeGuid, &subgroup, &setting, &value) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

} // namespace

ApplyResult SetScreenOffTimeouts(int acSeconds, int dcSeconds) {
    return WriteTimeouts(kVideoSubgroup, kVideoPowerdownTimeout, acSeconds, dcSeconds);
}

ApplyResult SetSleepTimeouts(int acSeconds, int dcSeconds) {
    return WriteTimeouts(kSleepSubgroup, kStandbyTimeout, acSeconds, dcSeconds);
}

ApplyResult SetHibernateTimeouts(int acSeconds, int dcSeconds) {
    return WriteTimeouts(kSleepSubgroup, kHibernateTimeout, acSeconds, dcSeconds);
}

std::optional<int> GetScreenOffTimeoutAc() { return ReadAcTimeout(kVideoSubgroup, kVideoPowerdownTimeout); }
std::optional<int> GetScreenOffTimeoutDc() { return ReadDcTimeout(kVideoSubgroup, kVideoPowerdownTimeout); }
std::optional<int> GetSleepTimeoutAc() { return ReadAcTimeout(kSleepSubgroup, kStandbyTimeout); }
std::optional<int> GetSleepTimeoutDc() { return ReadDcTimeout(kSleepSubgroup, kStandbyTimeout); }
std::optional<int> GetHibernateTimeoutAc() { return ReadAcTimeout(kSleepSubgroup, kHibernateTimeout); }
std::optional<int> GetHibernateTimeoutDc() { return ReadDcTimeout(kSleepSubgroup, kHibernateTimeout); }

} // namespace PowerTimeouts
} // namespace profiles
