#include "ShortcutStartupControl.h"
#include "../platform/StringConvert.h"
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <optional>
#include <string>

namespace startup {
namespace ShortcutStartupControl {

namespace {

using platform::WideToUtf8;

std::optional<std::wstring> GetKnownFolder(REFKNOWNFOLDERID folderId) {
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folderId, 0, nullptr, &path);
    if (FAILED(hr) || !path) {
        if (path) {
            CoTaskMemFree(path);
        }
        return std::nullopt;
    }
    std::wstring result(path);
    CoTaskMemFree(path);
    return result;
}

// Resolves a .lnk's target path via IShellLinkW/IPersistFile. Requires COM
// to already be initialized on this thread. Returns nullopt if the shortcut
// can't be loaded/resolved at all (e.g. it points at a non-file target like
// a folder or URL) -- the caller still lists the entry, just without a
// resolved exe path.
std::optional<std::wstring> ResolveShortcutTarget(const std::wstring& lnkPath) {
    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                   reinterpret_cast<void**>(&shellLink));
    if (FAILED(hr) || !shellLink) {
        return std::nullopt;
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
    if (FAILED(hr) || !persistFile) {
        shellLink->Release();
        return std::nullopt;
    }

    std::optional<std::wstring> result;
    if (SUCCEEDED(persistFile->Load(lnkPath.c_str(), STGM_READ))) {
        wchar_t targetPath[MAX_PATH] = {};
        WIN32_FIND_DATAW findData = {};
        if (SUCCEEDED(shellLink->GetPath(targetPath, MAX_PATH, &findData, SLGP_RAWPATH)) &&
            targetPath[0] != L'\0') {
            result = std::wstring(targetPath);
        }
    }

    persistFile->Release();
    shellLink->Release();
    return result;
}

void ScanLnkDirectory(const std::wstring& directory, StartupSource source, bool isDisabledSubfolder,
                       std::vector<StartupEntry>& outResult) {
    std::wstring searchPattern = directory + L"\\*.lnk";
    WIN32_FIND_DATAW findData;
    HANDLE findHandle = FindFirstFileW(searchPattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return; // folder missing (e.g. no "Disabled" subfolder yet) -- not an error
    }

    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        std::wstring fileName(findData.cFileName);
        std::wstring fullPath = directory + L"\\" + fileName;
        std::wstring nameWithoutExt = fileName.substr(0, fileName.size() - 4); // strip ".lnk"

        StartupEntry entry;
        entry.source = source;
        entry.displayName = WideToUtf8(nameWithoutExt);
        entry.id = (source == StartupSource::StartupFolderUser ? std::string("startupfolder-user:")
                                                                 : std::string("startupfolder-common:")) +
                   entry.displayName;
        entry.shortcutFilePath = fullPath;
        entry.enabled = !isDisabledSubfolder;

        std::optional<std::wstring> target = ResolveShortcutTarget(fullPath);
        if (target.has_value() && PathFileExistsW(target->c_str())) {
            entry.resolvedExePath = WideToUtf8(*target);
            entry.targetMissing = false;
        } else {
            entry.targetMissing = true;
        }

        outResult.push_back(std::move(entry));
    } while (FindNextFileW(findHandle, &findData));

    FindClose(findHandle);
}

std::vector<StartupEntry> EnumerateFolder(REFKNOWNFOLDERID folderId, StartupSource source) {
    std::vector<StartupEntry> result;
    std::optional<std::wstring> baseFolder = GetKnownFolder(folderId);
    if (!baseFolder.has_value()) {
        return result;
    }

    ScanLnkDirectory(*baseFolder, source, /*isDisabledSubfolder=*/false, result);
    ScanLnkDirectory(*baseFolder + L"\\Disabled", source, /*isDisabledSubfolder=*/true, result);
    return result;
}

} // namespace

std::vector<StartupEntry> EnumerateUserStartupFolder() {
    return EnumerateFolder(FOLDERID_Startup, StartupSource::StartupFolderUser);
}

std::vector<StartupEntry> EnumerateCommonStartupFolder() {
    return EnumerateFolder(FOLDERID_CommonStartup, StartupSource::StartupFolderCommon);
}

bool SetShortcutEnabled(StartupEntry& entry, bool enabled) {
    if (entry.shortcutFilePath.empty()) {
        return false;
    }

    size_t lastSlash = entry.shortcutFilePath.find_last_of(L'\\');
    if (lastSlash == std::wstring::npos) {
        return false;
    }
    std::wstring fileName = entry.shortcutFilePath.substr(lastSlash + 1);
    std::wstring currentDir = entry.shortcutFilePath.substr(0, lastSlash);

    bool currentlyInDisabledDir = currentDir.size() >= 9 &&
                                   currentDir.compare(currentDir.size() - 9, 9, L"\\Disabled") == 0;
    if (enabled == !currentlyInDisabledDir) {
        return true; // already in the desired location
    }

    std::wstring destinationDir;
    if (enabled) {
        // Currently in "...\Disabled" -- destination is its parent.
        destinationDir = currentDir.substr(0, currentDir.size() - 9);
    } else {
        destinationDir = currentDir + L"\\Disabled";
        if (!CreateDirectoryW(destinationDir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }

    std::wstring destinationPath = destinationDir + L"\\" + fileName;
    // MOVEFILE_REPLACE_EXISTING: a prior toggle can leave a stale file at the
    // destination (this module never deletes .lnk files), which would
    // otherwise make a normal enable/disable action fail.
    if (!MoveFileExW(entry.shortcutFilePath.c_str(), destinationPath.c_str(),
                      MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        return false;
    }

    entry.shortcutFilePath = destinationPath;
    entry.enabled = enabled;
    return true;
}

bool DeleteToRecycleBin(const StartupEntry& entry) {
    if (entry.shortcutFilePath.empty()) {
        return false;
    }
    if (!PathFileExistsW(entry.shortcutFilePath.c_str())) {
        return true; // already gone -- desired end state already holds
    }

    IFileOperation* fileOp = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_IFileOperation,
                                   reinterpret_cast<void**>(&fileOp));
    if (FAILED(hr) || !fileOp) {
        return false;
    }

    bool succeeded = false;
    // FOF_ALLOWUNDO: send to Recycle Bin, not a permanent delete.
    // FOF_NOCONFIRMATION/FOF_SILENT: this app already showed its own
    // confirmation dialog before calling this -- suppress Explorer's.
    if (SUCCEEDED(fileOp->SetOperationFlags(FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT))) {
        IShellItem* item = nullptr;
        hr = SHCreateItemFromParsingName(entry.shortcutFilePath.c_str(), nullptr, IID_IShellItem,
                                          reinterpret_cast<void**>(&item));
        if (SUCCEEDED(hr) && item) {
            if (SUCCEEDED(fileOp->DeleteItem(item, nullptr))) {
                succeeded = SUCCEEDED(fileOp->PerformOperations());
            }
            item->Release();
        }
    }
    fileOp->Release();
    return succeeded;
}

} // namespace ShortcutStartupControl
} // namespace startup
