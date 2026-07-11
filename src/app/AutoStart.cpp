#include "AutoStart.h"
#include <windows.h>
#include <string>

namespace app {
namespace AutoStart {

namespace {
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"MkPCApp";

std::wstring GetExePathQuoted() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"";
    }
    // --minimized keeps autostart from flashing the main window on login.
    return L"\"" + std::wstring(path, len) + L"\" --minimized";
}
} // namespace

bool IsEnabled() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[MAX_PATH * 2];
    DWORD size = sizeof(value);
    DWORD type = 0;
    LSTATUS status = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ;
}

void SetEnabled(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }

    if (enabled) {
        std::wstring exePath = GetExePathQuoted();
        if (!exePath.empty()) {
            RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath.c_str()),
                            static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, kValueName);
    }

    RegCloseKey(key);
}

} // namespace AutoStart
} // namespace app
