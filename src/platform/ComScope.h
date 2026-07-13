#pragma once
#include <windows.h>
#include <combaseapi.h>

namespace platform {

// Brackets a block of COM calls with exactly one CoInitializeEx/
// CoUninitialize pair on the current thread.
class ComScope {
public:
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // S_FALSE means COM was already initialized on this thread (by us or
        // someone else) in a compatible mode -- still must be paired with a
        // CoUninitialize call. RPC_E_CHANGED_MODE means a different
        // concurrency model is already set on this thread (e.g. by ImGui/DX11
        // internals) -- our call didn't take effect, so don't uninitialize.
        shouldUninitialize_ = (hr == S_OK || hr == S_FALSE);
    }

    ~ComScope() {
        if (shouldUninitialize_) {
            CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool shouldUninitialize_ = false;
};

} // namespace platform
