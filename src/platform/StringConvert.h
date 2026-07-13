#pragma once
#include <string>
#include <windows.h>

namespace platform {

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
    if (size <= 0) {
        return "";
    }
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return L"";
    }
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
    return result;
}

} // namespace platform
