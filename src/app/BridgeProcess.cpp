#include "BridgeProcess.h"
#include <chrono>
#include <vector>

namespace app {

namespace {
std::wstring ResolveBridgeExePath() {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"";
    }
    std::wstring exeDir(path, len);
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        return L"";
    }
    exeDir = exeDir.substr(0, lastSlash);
    return exeDir + L"\\sensor-bridge\\MkPCApp.SensorBridge.exe";
}
} // namespace

BridgeProcess::~BridgeProcess() {
    Stop();
}

void BridgeProcess::Start() {
    bridgeExePath_ = ResolveBridgeExePath();
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    jobObject_ = CreateJobObjectW(nullptr, nullptr);
    if (jobObject_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(jobObject_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    watcherThread_ = std::thread(&BridgeProcess::WatcherLoop, this);
}

void BridgeProcess::Stop() {
    stopping_.store(true);
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }

    if (watcherThread_.joinable()) {
        watcherThread_.join();
    }

    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (jobObject_) {
        CloseHandle(jobObject_); // also terminates any bridge process still assigned to it
        jobObject_ = nullptr;
    }
}

bool BridgeProcess::SpawnBridge() {
    if (bridgeExePath_.empty()) {
        return false;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    std::wstring commandLine = L"\"" + bridgeExePath_ + L"\"";
    std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
    mutableCmd.push_back(L'\0');

    BOOL ok = CreateProcessW(bridgeExePath_.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS, nullptr, nullptr, &startupInfo,
                              &processInfo);
    if (!ok) {
        return false;
    }

    if (jobObject_) {
        AssignProcessToJobObject(jobObject_, processInfo.hProcess);
    }

    std::lock_guard<std::mutex> lock(processMutex_);
    processInfo_ = processInfo;
    return true;
}

void BridgeProcess::WatcherLoop() {
    int retriesInWindow = 0;
    ULONGLONG windowStartMs = GetTickCount64();

    while (!stopping_.load()) {
        if (!SpawnBridge()) {
            available_.store(false);
            return; // exe missing/corrupt; give up without retry-looping
        }
        available_.store(true);

        HANDLE processHandle;
        {
            std::lock_guard<std::mutex> lock(processMutex_);
            processHandle = processInfo_.hProcess;
        }

        // Wait on the process AND stopEvent_ together: Stop() may run at any
        // point relative to this spawn, so we must not rely on it already
        // having a valid handle to terminate — it wakes us here instead.
        HANDLE waitHandles[2] = {processHandle, stopEvent_};
        DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

        if (waitResult == WAIT_OBJECT_0 + 1) {
            // stopEvent_ signaled: terminate whatever is currently running and exit.
            std::lock_guard<std::mutex> lock(processMutex_);
            TerminateProcess(processInfo_.hProcess, 0);
            CloseHandle(processInfo_.hProcess);
            CloseHandle(processInfo_.hThread);
            processInfo_ = {};
            available_.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(processMutex_);
            CloseHandle(processInfo_.hProcess);
            CloseHandle(processInfo_.hThread);
            processInfo_ = {};
        }
        available_.store(false);

        if (stopping_.load()) {
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (now - windowStartMs > static_cast<ULONGLONG>(kRetryWindowSeconds) * 1000) {
            windowStartMs = now;
            retriesInWindow = 0;
        }

        if (++retriesInWindow > kMaxRetries) {
            return; // crash-looping; stop retrying for this session
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace app
