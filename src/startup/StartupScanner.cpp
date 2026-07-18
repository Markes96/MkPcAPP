#include "StartupScanner.h"
#include "ShortcutStartupControl.h"
#include "../platform/ComScope.h"
#include "../platform/StringConvert.h"

namespace startup {

namespace {

using platform::ComScope;
using platform::Utf8ToWide;

std::wstring QuoteIfNeeded(const std::wstring& path) {
    if (path.empty() || path.front() == L'"') {
        return path;
    }
    if (path.find(L' ') != std::wstring::npos) {
        return L"\"" + path + L"\"";
    }
    return path;
}

bool PathStartsWithDir(const std::wstring& path, const wchar_t* dir, DWORD dirLen) {
    if (dirLen == 0 || dirLen >= MAX_PATH) {
        return false;
    }
    std::wstring dirWithSep(dir, dirLen);
    if (dirWithSep.back() != L'\\') {
        dirWithSep += L'\\';
    }
    if (path.size() < dirWithSep.size()) {
        return false;
    }
    return _wcsnicmp(path.c_str(), dirWithSep.c_str(), dirWithSep.size()) == 0;
}

// GetSystemWow64DirectoryW covers 32-bit binaries redirected to SysWOW64 on
// 64-bit Windows -- both are real "system" locations a legitimate
// third-party installer has no business writing to without elevation.
bool IsUnderSystemDirectory(const std::wstring& exePath) {
    wchar_t systemDir[MAX_PATH];
    DWORD systemDirLen = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (systemDirLen != 0 && PathStartsWithDir(exePath, systemDir, systemDirLen)) {
        return true;
    }

    wchar_t wow64Dir[MAX_PATH];
    DWORD wow64DirLen = GetSystemWow64DirectoryW(wow64Dir, MAX_PATH);
    if (wow64DirLen != 0 && PathStartsWithDir(exePath, wow64Dir, wow64DirLen)) {
        return true;
    }

    return false;
}

} // namespace

ScanResult StartupScanner::Scan() {
    ScanResult result;
    // ShortcutStartupControl needs COM (IShellLink) for the whole scan, not
    // per-call -- bracket it once here rather than per shortcut resolved.
    ComScope comScope;

    std::vector<StartupEntry> all = RegistryStartupControl::EnumerateHkcuRun();
    std::vector<StartupEntry> hklm = RegistryStartupControl::EnumerateHklmRun();
    std::vector<StartupEntry> userFolder = ShortcutStartupControl::EnumerateUserStartupFolder();
    std::vector<StartupEntry> commonFolder = ShortcutStartupControl::EnumerateCommonStartupFolder();
    all.insert(all.end(), hklm.begin(), hklm.end());
    all.insert(all.end(), userFolder.begin(), userFolder.end());
    all.insert(all.end(), commonFolder.begin(), commonFolder.end());

    // signatureVerifier_ is the only state shared with the main-thread
    // AddManualEntry/DeleteEntry/GetSignatureInfo calls -- lock only around
    // touching it, not around the registry/filesystem enumeration itself
    // (this function's slow part, which doesn't touch shared state).
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : all) {
        // A missing target can't actually run at logon, so there's nothing
        // for "disable" to do -- listing it as a candidate would just
        // mislead a non-technical user into thinking they're stopping
        // something active. Excluded the same way as Microsoft-signed
        // entries: entirely, never just greyed out.
        if (entry.targetMissing) {
            continue;
        }
        std::wstring wideExePath = Utf8ToWide(entry.resolvedExePath);
        bool isMicrosoft = signatureVerifier_.IsMicrosoftSigned(wideExePath);
        if (isMicrosoft) {
            continue; // excluded entirely, never just greyed out
        }
        entry.isUnderSystem32 = IsUnderSystemDirectory(wideExePath);
        result.entries.push_back(std::move(entry));
    }

    return result;
}

bool StartupScanner::SetEnabled(StartupEntry& entry, bool enabled) {
    bool succeeded = false;
    switch (entry.source) {
        case StartupSource::RegistryHkcuRun:
            succeeded = RegistryStartupControl::SetApprovedEnabled(HKEY_CURRENT_USER, entry.registryValueName,
                                                                      entry.isWow6432, enabled);
            break;
        case StartupSource::RegistryHklmRun:
            succeeded = RegistryStartupControl::SetApprovedEnabled(HKEY_LOCAL_MACHINE, entry.registryValueName,
                                                                      entry.isWow6432, enabled);
            break;
        case StartupSource::StartupFolderUser:
        case StartupSource::StartupFolderCommon:
            // SetShortcutEnabled updates entry.enabled itself on success.
            return ShortcutStartupControl::SetShortcutEnabled(entry, enabled);
    }
    if (succeeded) {
        entry.enabled = enabled;
    }
    return succeeded;
}

RegistryStartupControl::AddResult StartupScanner::AddManualEntry(const std::wstring& displayName,
                                                                    const std::wstring& exePath) {
    return RegistryStartupControl::AddUserRunEntry(displayName, QuoteIfNeeded(exePath));
}

bool StartupScanner::DeleteEntry(const StartupEntry& entry) {
    switch (entry.source) {
        case StartupSource::RegistryHkcuRun:
            return RegistryStartupControl::DeleteRunEntry(HKEY_CURRENT_USER, entry.registryValueName,
                                                             entry.isWow6432);
        case StartupSource::RegistryHklmRun:
            return RegistryStartupControl::DeleteRunEntry(HKEY_LOCAL_MACHINE, entry.registryValueName,
                                                             entry.isWow6432);
        case StartupSource::StartupFolderUser:
        case StartupSource::StartupFolderCommon: {
            // Recycle-bin deletion needs COM (IFileOperation), bracketed
            // locally since this is a rare, user-triggered one-off action,
            // not part of Scan()'s per-cycle ComScope pairing.
            ComScope comScope;
            return ShortcutStartupControl::DeleteToRecycleBin(entry);
        }
    }
    return false;
}

SignatureInfo StartupScanner::GetSignatureInfo(const std::wstring& exePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    return signatureVerifier_.GetSignatureInfo(exePath);
}

} // namespace startup
