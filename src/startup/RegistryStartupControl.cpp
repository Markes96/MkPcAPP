#include "RegistryStartupControl.h"
#include "../platform/StringConvert.h"
#include <shlwapi.h>
#include <vector>
#include <string>

namespace startup {
namespace RegistryStartupControl {

namespace {

using platform::WideToUtf8;

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunKeyPathWow6432[] =
    L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kApprovedRunKeyPathWow6432[] =
    L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

// Splits an exe path out of a Run value's command line, handling both
// quoted ("C:\Program Files\App\app.exe" --flag) and unquoted
// (C:\App\app.exe --flag, where "--flag" itself may contain spaces) forms.
// For the unquoted+ambiguous case, tries progressively longer prefixes
// ending right before each space until one names a real file. Never fails
// outright -- worst case returns the first whitespace-delimited token, since
// the entry must still be listed (with targetMissing set by the caller) even
// when nothing on disk matches.
std::wstring ParseExecutablePath(const std::wstring& commandLine) {
    size_t start = commandLine.find_first_not_of(L" \t");
    if (start == std::wstring::npos) {
        return L"";
    }
    std::wstring trimmed = commandLine.substr(start);

    if (!trimmed.empty() && trimmed.front() == L'"') {
        size_t closingQuote = trimmed.find(L'"', 1);
        if (closingQuote != std::wstring::npos) {
            return trimmed.substr(1, closingQuote - 1);
        }
        // Unterminated quote (malformed data) -- fall through and treat the
        // rest as unquoted.
        trimmed = trimmed.substr(1);
    }

    size_t searchPos = 0;
    while (true) {
        size_t spacePos = trimmed.find(L' ', searchPos);
        std::wstring candidate = (spacePos == std::wstring::npos) ? trimmed : trimmed.substr(0, spacePos);
        if (!candidate.empty() && PathFileExistsW(candidate.c_str())) {
            return candidate;
        }
        if (spacePos == std::wstring::npos) {
            break;
        }
        searchPos = spacePos + 1;
    }

    size_t firstSpace = trimmed.find(L' ');
    return (firstSpace == std::wstring::npos) ? trimmed : trimmed.substr(0, firstSpace);
}

bool IsApprovedEnabled(HKEY hive, const wchar_t* approvedPath, const std::wstring& valueName) {
    HKEY key;
    if (RegOpenKeyExW(hive, approvedPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return true; // key missing -- Windows' own default is "enabled"
    }

    BYTE buffer[16] = {0};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, buffer, &size);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_BINARY || size == 0) {
        return true; // value missing -- default is "enabled"
    }
    return buffer[0] == 0x06 || buffer[0] == 0x07;
}

std::vector<StartupEntry> EnumerateRunKeyValues(HKEY hive, const wchar_t* runPath,
                                                  const wchar_t* approvedPath, StartupSource source,
                                                  bool isWow6432) {
    std::vector<StartupEntry> result;

    HKEY key;
    if (RegOpenKeyExW(hive, runPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return result;
    }

    wchar_t valueName[256];
    BYTE valueData[2048];
    DWORD index = 0;
    while (true) {
        DWORD nameSize = ARRAYSIZE(valueName);
        DWORD dataSize = sizeof(valueData);
        DWORD type = 0;
        LSTATUS status = RegEnumValueW(key, index, valueName, &nameSize, nullptr, &type, valueData, &dataSize);
        if (status == ERROR_NO_MORE_ITEMS) {
            break;
        }
        ++index;
        if (status != ERROR_SUCCESS) {
            continue;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) {
            continue;
        }

        // REG_SZ data isn't guaranteed to be null-terminated within the
        // buffer if it exactly fills it -- build the wstring from the
        // reported byte count instead of relying on an embedded NUL, then
        // trim any trailing NUL(s) the registry did include.
        size_t charCount = dataSize / sizeof(wchar_t);
        std::wstring commandLine(reinterpret_cast<wchar_t*>(valueData), charCount);
        size_t nulPos = commandLine.find(L'\0');
        if (nulPos != std::wstring::npos) {
            commandLine.resize(nulPos);
        }
        std::wstring exePath = ParseExecutablePath(commandLine);

        StartupEntry entry;
        entry.source = source;
        entry.isWow6432 = isWow6432;
        entry.registryValueName = valueName;
        entry.displayName = WideToUtf8(valueName);
        entry.id = (hive == HKEY_CURRENT_USER ? std::string("hkcu:") : std::string("hklm:")) +
                   (isWow6432 ? "wow6432:" : "") + entry.displayName;
        entry.resolvedExePath = WideToUtf8(exePath);
        entry.targetMissing = exePath.empty() || !PathFileExistsW(exePath.c_str());
        entry.enabled = IsApprovedEnabled(hive, approvedPath, valueName);
        result.push_back(std::move(entry));
    }

    RegCloseKey(key);
    return result;
}

} // namespace

std::vector<StartupEntry> EnumerateHkcuRun() {
    return EnumerateRunKeyValues(HKEY_CURRENT_USER, kRunKeyPath, kApprovedRunKeyPath,
                                  StartupSource::RegistryHkcuRun, /*isWow6432=*/false);
}

std::vector<StartupEntry> EnumerateHklmRun() {
    std::vector<StartupEntry> result =
        EnumerateRunKeyValues(HKEY_LOCAL_MACHINE, kRunKeyPath, kApprovedRunKeyPath,
                               StartupSource::RegistryHklmRun, /*isWow6432=*/false);
    std::vector<StartupEntry> wow6432 =
        EnumerateRunKeyValues(HKEY_LOCAL_MACHINE, kRunKeyPathWow6432, kApprovedRunKeyPathWow6432,
                               StartupSource::RegistryHklmRun, /*isWow6432=*/true);
    result.insert(result.end(), wow6432.begin(), wow6432.end());
    return result;
}

bool SetApprovedEnabled(HKEY hive, const std::wstring& valueName, bool isWow6432, bool enabled) {
    const wchar_t* approvedPath = isWow6432 ? kApprovedRunKeyPathWow6432 : kApprovedRunKeyPath;

    HKEY key;
    if (RegCreateKeyExW(hive, approvedPath, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    BYTE buffer[16] = {0};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LSTATUS readStatus = RegQueryValueExW(key, valueName.c_str(), nullptr, &type, buffer, &size);
    if (readStatus != ERROR_SUCCESS || type != REG_BINARY || size == 0) {
        // No existing blob to preserve -- write a minimal one Task Manager
        // itself tolerates.
        ZeroMemory(buffer, sizeof(buffer));
        size = 8;
    }
    buffer[0] = enabled ? 0x06 : 0x03;

    LSTATUS writeStatus = RegSetValueExW(key, valueName.c_str(), 0, REG_BINARY, buffer, size);
    RegCloseKey(key);
    return writeStatus == ERROR_SUCCESS;
}

AddResult AddUserRunEntry(const std::wstring& displayName, const std::wstring& quotedExePath) {
    if (displayName.empty()) {
        return AddResult::Failed;
    }

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key,
                         nullptr) != ERROR_SUCCESS) {
        return AddResult::Failed;
    }

    // Passing a null data buffer/size just checks whether the value exists
    // (and returns its size/type), without needing a buffer sized to fit a
    // real command line -- a fixed-size buffer here would make
    // RegQueryValueExW fail with ERROR_MORE_DATA for any realistic path and
    // silently skip the duplicate check.
    if (RegQueryValueExW(key, displayName.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        RegCloseKey(key);
        return AddResult::DuplicateName;
    }

    LSTATUS status =
        RegSetValueExW(key, displayName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(quotedExePath.c_str()),
                       static_cast<DWORD>((quotedExePath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return status == ERROR_SUCCESS ? AddResult::Ok : AddResult::Failed;
}

bool DeleteUserRunEntry(const std::wstring& valueName) {
    HKEY runKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &runKey) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status = RegDeleteValueW(runKey, valueName.c_str());
    RegCloseKey(runKey);

    // Best-effort cleanup of the matching StartupApproved state, ignored on
    // failure -- an orphaned approved-state blob for a value that no longer
    // exists is harmless (Explorer only consults it alongside a live Run
    // value).
    HKEY approvedKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedRunKeyPath, 0, KEY_SET_VALUE, &approvedKey) ==
        ERROR_SUCCESS) {
        RegDeleteValueW(approvedKey, valueName.c_str());
        RegCloseKey(approvedKey);
    }

    return status == ERROR_SUCCESS;
}

} // namespace RegistryStartupControl
} // namespace startup
