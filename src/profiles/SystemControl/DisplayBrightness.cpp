#include "DisplayBrightness.h"
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <vector>

namespace profiles {
namespace DisplayBrightness {

namespace {

// Internal-panel brightness via WMI (ROOT\WMI, WmiMonitorBrightness /
// WmiMonitorBrightnessMethods). Only laptops/tablets with an ACPI-exposed
// panel typically have an instance of these classes — a desktop with only
// external monitors simply has no such instance, which is treated as "this
// surface is absent" rather than a failure.
//
// COM is assumed already initialized (apartment-threaded) on the calling
// thread by ProfileManager — CoInitializeEx/CoUninitialize are deliberately
// not called here.
bool ConnectWmiNamespace(IWbemServices** outServices) {
    IWbemLocator* locator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                   reinterpret_cast<void**>(&locator));
    if (FAILED(hr) || !locator) {
        return false;
    }

    IWbemServices* services = nullptr;
    hr = locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                 &services);
    locator->Release();
    if (FAILED(hr) || !services) {
        return false;
    }

    hr = CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        services->Release();
        return false;
    }

    *outServices = services;
    return true;
}

bool TryGetWmiInternalBrightness(int& outPercent) {
    IWbemServices* services = nullptr;
    if (!ConnectWmiNamespace(&services)) {
        return false;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr =
        services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(L"SELECT * FROM WmiMonitorBrightness"),
                             WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr) || !enumerator) {
        services->Release();
        return false;
    }

    IWbemClassObject* instance = nullptr;
    ULONG returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &instance, &returned);
    enumerator->Release();
    if (FAILED(hr) || returned == 0 || !instance) {
        services->Release();
        return false;
    }

    VARIANT value;
    VariantInit(&value);
    bool ok = false;
    if (SUCCEEDED(instance->Get(L"CurrentBrightness", 0, &value, nullptr, nullptr))) {
        outPercent = static_cast<int>(value.bVal);
        ok = true;
    }
    VariantClear(&value);
    instance->Release();
    services->Release();
    return ok;
}

bool TrySetWmiInternalBrightness(int percent) {
    IWbemServices* services = nullptr;
    if (!ConnectWmiNamespace(&services)) {
        return false;
    }

    IEnumWbemClassObject* enumerator = nullptr;
    HRESULT hr = services->ExecQuery(
        _bstr_t(L"WQL"), _bstr_t(L"SELECT * FROM WmiMonitorBrightnessMethods"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    if (FAILED(hr) || !enumerator) {
        services->Release();
        return false;
    }

    IWbemClassObject* instance = nullptr;
    ULONG returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &instance, &returned);
    enumerator->Release();
    if (FAILED(hr) || returned == 0 || !instance) {
        services->Release();
        return false;
    }

    bool ok = false;
    VARIANT pathVariant;
    VariantInit(&pathVariant);
    if (SUCCEEDED(instance->Get(L"__PATH", 0, &pathVariant, nullptr, nullptr)) &&
        pathVariant.vt == VT_BSTR) {
        IWbemClassObject* classDef = nullptr;
        if (SUCCEEDED(services->GetObject(_bstr_t(L"WmiMonitorBrightnessMethods"), 0, nullptr, &classDef,
                                           nullptr)) &&
            classDef) {
            IWbemClassObject* inParamsDef = nullptr;
            if (SUCCEEDED(classDef->GetMethod(L"WmiSetBrightness", 0, &inParamsDef, nullptr)) &&
                inParamsDef) {
                IWbemClassObject* inParams = nullptr;
                if (SUCCEEDED(inParamsDef->SpawnInstance(0, &inParams)) && inParams) {
                    VARIANT timeoutVariant;
                    VariantInit(&timeoutVariant);
                    timeoutVariant.vt = VT_I4;
                    timeoutVariant.lVal = 0;
                    inParams->Put(L"Timeout", 0, &timeoutVariant, 0);
                    VariantClear(&timeoutVariant);

                    VARIANT brightnessVariant;
                    VariantInit(&brightnessVariant);
                    brightnessVariant.vt = VT_UI1;
                    brightnessVariant.bVal = static_cast<BYTE>(percent);
                    inParams->Put(L"Brightness", 0, &brightnessVariant, 0);
                    VariantClear(&brightnessVariant);

                    IWbemClassObject* outParams = nullptr;
                    HRESULT execHr = services->ExecMethod(pathVariant.bstrVal, _bstr_t(L"WmiSetBrightness"),
                                                           0, nullptr, inParams, &outParams, nullptr);
                    if (outParams) {
                        outParams->Release();
                    }
                    ok = SUCCEEDED(execHr);
                    inParams->Release();
                }
                inParamsDef->Release();
            }
            classDef->Release();
        }
    }
    VariantClear(&pathVariant);
    instance->Release();
    services->Release();
    return ok;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    auto* monitors = reinterpret_cast<std::vector<HMONITOR>*>(lParam);
    monitors->push_back(hMonitor);
    return TRUE;
}

std::vector<HMONITOR> EnumerateMonitors() {
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}

// VCP code 0x10 is the DDC/CI standard "luminance" (brightness) feature, but
// its actual maximum is monitor-defined and not guaranteed to be 100 --
// these convert percent<->raw VCP value using the monitor's real max instead
// of writing/reading a raw 0-100 value (silently wrong/clamped on a monitor
// whose max isn't 100). Shared by the set and get paths below so the two
// directions of the same conversion can't drift apart.
DWORD ScalePercentToVcp(int percent, DWORD maxValue) {
    return static_cast<DWORD>((static_cast<long long>(percent) * maxValue + 50) / 100);
}

int ScaleVcpToPercent(DWORD value, DWORD maxValue) {
    return static_cast<int>((static_cast<long long>(value) * 100 + maxValue / 2) / maxValue);
}

// Opens hMonitor's physical-monitor handle list (if any), runs `action` over
// it, and always destroys the handles afterward -- shared by the set and get
// paths below so the open/destroy lifecycle can't drift between them.
template <typename Action>
void WithPhysicalMonitors(HMONITOR hMonitor, Action action) {
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &count) || count == 0) {
        return;
    }

    std::vector<PHYSICAL_MONITOR> physicalMonitors(count);
    if (!GetPhysicalMonitorsFromHMONITOR(hMonitor, count, physicalMonitors.data())) {
        return;
    }

    action(physicalMonitors.data(), count);
    DestroyPhysicalMonitors(count, physicalMonitors.data());
}

// Returns true if at least one physical monitor's VCP write succeeded.
// `outAnyMonitorFound` is set if any physical monitor handle was opened at
// all, regardless of whether the write itself succeeded on it — used by the
// caller to tell "no external monitors" apart from "monitor present but
// rejected DDC/CI".
bool TrySetExternalBrightness(int percent, bool& outAnyMonitorFound) {
    bool anySucceeded = false;
    outAnyMonitorFound = false;

    for (HMONITOR hMonitor : EnumerateMonitors()) {
        WithPhysicalMonitors(hMonitor, [&](PHYSICAL_MONITOR* monitors, DWORD count) {
            for (DWORD i = 0; i < count; ++i) {
                outAnyMonitorFound = true;
                DWORD currentValue = 0;
                DWORD maxValue = 0;
                DWORD valueToWrite = static_cast<DWORD>(percent);
                if (GetVCPFeatureAndVCPFeatureReply(monitors[i].hPhysicalMonitor, 0x10, nullptr, &currentValue,
                                                     &maxValue) &&
                    maxValue > 0) {
                    valueToWrite = ScalePercentToVcp(percent, maxValue);
                }
                if (SetVCPFeature(monitors[i].hPhysicalMonitor, 0x10, valueToWrite)) {
                    anySucceeded = true;
                }
            }
        });
    }

    return anySucceeded;
}

std::optional<int> TryGetExternalBrightness() {
    std::optional<int> result;

    for (HMONITOR hMonitor : EnumerateMonitors()) {
        if (result.has_value()) {
            break;
        }
        WithPhysicalMonitors(hMonitor, [&](PHYSICAL_MONITOR* monitors, DWORD count) {
            for (DWORD i = 0; i < count && !result.has_value(); ++i) {
                DWORD current = 0;
                DWORD maxValue = 0;
                if (GetVCPFeatureAndVCPFeatureReply(monitors[i].hPhysicalMonitor, 0x10, nullptr, &current,
                                                     &maxValue) &&
                    maxValue > 0) {
                    // Normalize to 0-100 using the monitor's real max,
                    // rather than returning the raw VCP value as if it were
                    // already a percent.
                    result = ScaleVcpToPercent(current, maxValue);
                }
            }
        });
    }

    return result;
}

} // namespace

ApplyResult SetBrightness(int percent) {
    bool wmiOk = TrySetWmiInternalBrightness(percent);

    bool anyExternalFound = false;
    bool externalOk = TrySetExternalBrightness(percent, anyExternalFound);

    if (wmiOk || externalOk) {
        return ApplyResult::Ok;
    }
    return ApplyResult::Unsupported;
}

std::optional<int> GetBrightness() {
    int internal = 0;
    if (TryGetWmiInternalBrightness(internal)) {
        return internal;
    }
    return TryGetExternalBrightness();
}

} // namespace DisplayBrightness
} // namespace profiles
