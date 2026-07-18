#include "GroupStore.h"
#include "GroupJson.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace groups {
namespace GroupStore {

namespace {

bool GetLocalAppDataDir(std::wstring& outDir) {
    PWSTR localAppData = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData);
    if (FAILED(hr) || !localAppData) {
        if (localAppData) {
            CoTaskMemFree(localAppData);
        }
        return false;
    }
    outDir = std::wstring(localAppData) + L"\\MkPCApp";
    CoTaskMemFree(localAppData);
    return true;
}

bool EnsureDirectoryExists(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

// Empty string means "unavailable" (known-folder lookup failed or the
// directory couldn't be created) -- callers treat that as a no-op.
std::wstring GetGroupsFilePath() {
    std::wstring dir;
    if (!GetLocalAppDataDir(dir)) {
        return L"";
    }
    if (!EnsureDirectoryExists(dir)) {
        return L"";
    }
    return dir + L"\\groups.json";
}

std::string ReadFileContents() {
    std::wstring path = GetGroupsFilePath();
    if (path.empty()) {
        return "";
    }

    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void WriteFileContents(const std::string& contents) {
    std::wstring path = GetGroupsFilePath();
    if (path.empty()) {
        return;
    }

    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }
    file << contents;
}

} // namespace

std::vector<LaunchGroup> Load() {
    std::vector<LaunchGroup> empty;

    std::string contents = ReadFileContents();
    if (contents.empty()) {
        return empty;
    }

    std::vector<LaunchGroup> parsed;
    if (!DeserializeGroups(contents, parsed)) {
        return empty;
    }
    return parsed;
}

void Save(const std::vector<LaunchGroup>& groups) {
    WriteFileContents(SerializeGroups(groups));
}

} // namespace GroupStore
} // namespace groups
