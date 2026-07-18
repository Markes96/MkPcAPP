#include "ProcessLifecycle.h"
#include <tlhelp32.h>
#include <vector>

namespace groups {

namespace {

bool PathsEqual(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

struct EnumWindowsContext {
    DWORD targetPid;
    std::vector<HWND> windows;
};

BOOL CALLBACK CollectTopLevelWindowsForPid(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumWindowsContext*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == ctx->targetPid && IsWindowVisible(hwnd)) {
        ctx->windows.push_back(hwnd);
    }
    return TRUE;
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t lastSlash = path.find_last_of(L"\\/");
    return (lastSlash == std::wstring::npos) ? L"" : path.substr(0, lastSlash);
}

} // namespace

bool IsProcessRunning(const std::wstring& resolvedExePath) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool found = false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) {
                continue;
            }
            wchar_t imagePath[MAX_PATH];
            DWORD imagePathLen = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, imagePath, &imagePathLen) &&
                PathsEqual(std::wstring(imagePath, imagePathLen), resolvedExePath)) {
                found = true;
            }
            CloseHandle(process);
        } while (!found && Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

LaunchResult LaunchProcess(const std::wstring& exePath, const std::wstring& args) {
    LaunchResult result;

    std::wstring commandLine = L"\"" + exePath + L"\"";
    if (!args.empty()) {
        commandLine += L" " + args;
    }
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    std::wstring workingDir = DirectoryOf(exePath);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    BOOL ok = CreateProcessW(exePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                              workingDir.empty() ? nullptr : workingDir.c_str(), &startupInfo, &processInfo);
    if (!ok) {
        return result;
    }

    CloseHandle(processInfo.hThread);
    result.ok = true;
    result.pid = processInfo.dwProcessId;
    result.processHandle = processInfo.hProcess;
    return result;
}

void RequestGracefulClose(DWORD pid) {
    EnumWindowsContext ctx{pid, {}};
    EnumWindows(CollectTopLevelWindowsForPid, reinterpret_cast<LPARAM>(&ctx));
    for (HWND hwnd : ctx.windows) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

bool IsProcessAlive(HANDLE processHandle) {
    if (!processHandle) {
        return false;
    }
    return WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT;
}

bool ForceTerminate(HANDLE processHandle) {
    if (!processHandle) {
        return true;
    }
    if (!IsProcessAlive(processHandle)) {
        return true;
    }
    return TerminateProcess(processHandle, 1) != 0;
}

} // namespace groups
