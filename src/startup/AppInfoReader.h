#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace startup {

struct AppInfo {
    // Empty optional means "couldn't be read" -- callers show "No
    // disponible" rather than leaving a blank field, per the app's
    // degrade-visibly principle.
    std::optional<uint64_t> fileSizeBytes;
    std::optional<std::string> productVersion;
    std::optional<std::string> fileDescription;
};

// Pure Win32 (file attributes + VERSIONINFO resource), no ImGui/D3D
// knowledge -- reviewable independent of rendering, same split as
// IconExtractor. Never fails outright: a missing/malformed VERSIONINFO
// block or an inaccessible file just leaves the corresponding field empty.
AppInfo ReadAppInfo(const std::wstring& exePath);

} // namespace startup
