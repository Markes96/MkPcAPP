#include "ProfileStore.h"
#include "ProfileJson.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

namespace profiles {
namespace ProfileStore {

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
// directory couldn't be created) — callers treat that as a no-op.
std::wstring GetProfilesFilePath() {
    std::wstring dir;
    if (!GetLocalAppDataDir(dir)) {
        return L"";
    }
    if (!EnsureDirectoryExists(dir)) {
        return L"";
    }
    return dir + L"\\profiles.json";
}

} // namespace

namespace {

// Empty string means "file missing/unreadable" — callers treat that the same
// as an empty document (zero profiles, zero rules).
std::string ReadFileContents() {
    std::wstring path = GetProfilesFilePath();
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
    std::wstring path = GetProfilesFilePath();
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

std::vector<Profile> Load() {
    std::vector<Profile> empty;

    std::string contents = ReadFileContents();
    if (contents.empty()) {
        return empty;
    }

    std::vector<Profile> parsed;
    if (!DeserializeProfiles(contents, parsed)) {
        return empty;
    }
    return parsed;
}

void Save(const std::vector<Profile>& customProfiles) {
    // Read whatever rules already live in the file so this write doesn't
    // clobber them -- profiles and rules share one on-disk document.
    std::vector<AutomationRule> existingRules = LoadRules();
    WriteFileContents(SerializeDocument(customProfiles, existingRules));
}

std::vector<AutomationRule> LoadRules() {
    std::vector<AutomationRule> empty;

    std::string contents = ReadFileContents();
    if (contents.empty()) {
        return empty;
    }

    std::vector<AutomationRule> parsed;
    if (!DeserializeRules(contents, parsed)) {
        return empty;
    }
    return parsed;
}

void SaveRules(const std::vector<AutomationRule>& rules) {
    std::vector<Profile> existingProfiles = Load();
    WriteFileContents(SerializeDocument(existingProfiles, rules));
}

} // namespace ProfileStore
} // namespace profiles
