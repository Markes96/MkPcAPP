#include "AppInfoReader.h"
#include "../platform/StringConvert.h"
#include <windows.h>
#include <cwchar>
#include <vector>

namespace startup {

namespace {

std::optional<uint64_t> ReadFileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return std::nullopt;
    }
    ULARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

// Reads a single string value (e.g. "ProductVersion", "FileDescription")
// out of the exe's VERSIONINFO resource for the first language/codepage
// block it finds -- most executables only ship one, and falling back to
// "whichever is first" degrades gracefully instead of requiring an exact
// language match.
std::optional<std::string> ReadVersionString(std::vector<BYTE>& versionData, const wchar_t* stringName) {
    struct LangAndCodepage {
        WORD language;
        WORD codepage;
    };
    LangAndCodepage* translations = nullptr;
    UINT translationsSize = 0;
    if (!VerQueryValueW(versionData.data(), L"\\VarFileInfo\\Translation",
                         reinterpret_cast<void**>(&translations), &translationsSize) ||
        translationsSize < sizeof(LangAndCodepage)) {
        return std::nullopt;
    }

    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codepage,
               stringName);

    wchar_t* value = nullptr;
    UINT valueSize = 0;
    if (!VerQueryValueW(versionData.data(), subBlock, reinterpret_cast<void**>(&value), &valueSize) ||
        valueSize == 0 || !value) {
        return std::nullopt;
    }

    return platform::WideToUtf8(std::wstring(value));
}

} // namespace

AppInfo ReadAppInfo(const std::wstring& exePath) {
    AppInfo info;
    if (exePath.empty()) {
        return info;
    }

    info.fileSizeBytes = ReadFileSize(exePath);

    DWORD handle = 0;
    DWORD versionSize = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (versionSize == 0) {
        return info; // no VERSIONINFO block -- version/description stay empty
    }

    std::vector<BYTE> versionData(versionSize);
    if (!GetFileVersionInfoW(exePath.c_str(), handle, versionSize, versionData.data())) {
        return info;
    }

    info.productVersion = ReadVersionString(versionData, L"ProductVersion");
    info.fileDescription = ReadVersionString(versionData, L"FileDescription");
    return info;
}

} // namespace startup
