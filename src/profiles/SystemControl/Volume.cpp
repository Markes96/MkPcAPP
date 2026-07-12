#include "Volume.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

namespace profiles {
namespace Volume {

namespace {

// COM is assumed already initialized (apartment-threaded) on the calling
// thread by ProfileManager — CoInitializeEx/CoUninitialize are deliberately
// not called here.
bool GetDefaultEndpointVolume(IAudioEndpointVolume** outEndpointVolume) {
    IMMDeviceEnumerator* enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) {
        return false;
    }

    IMMDevice* device = nullptr;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    enumerator->Release();
    if (FAILED(hr) || !device) {
        return false;
    }

    IAudioEndpointVolume* endpointVolume = nullptr;
    hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&endpointVolume));
    device->Release();
    if (FAILED(hr) || !endpointVolume) {
        return false;
    }

    *outEndpointVolume = endpointVolume;
    return true;
}

} // namespace

ApplyResult SetVolume(int percent) {
    IAudioEndpointVolume* endpointVolume = nullptr;
    if (!GetDefaultEndpointVolume(&endpointVolume)) {
        return ApplyResult::Failed;
    }

    float clamped = percent < 0 ? 0.0f : (percent > 100 ? 100.0f : static_cast<float>(percent));
    HRESULT hr = endpointVolume->SetMasterVolumeLevelScalar(clamped / 100.0f, nullptr);
    endpointVolume->Release();

    return SUCCEEDED(hr) ? ApplyResult::Ok : ApplyResult::Failed;
}

std::optional<int> GetVolume() {
    IAudioEndpointVolume* endpointVolume = nullptr;
    if (!GetDefaultEndpointVolume(&endpointVolume)) {
        return std::nullopt;
    }

    float scalar = 0.0f;
    HRESULT hr = endpointVolume->GetMasterVolumeLevelScalar(&scalar);
    endpointVolume->Release();
    if (FAILED(hr)) {
        return std::nullopt;
    }

    return static_cast<int>(scalar * 100.0f + 0.5f);
}

} // namespace Volume
} // namespace profiles
