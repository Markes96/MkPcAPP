#pragma once
#include <windows.h>
#include <d3d11.h>

namespace platform {

// Owns the D3D11 device/swapchain/render-target used to render Dear ImGui.
// Honors DXGI occlusion (Section 7 of the design: skip presentation work when
// the window isn't actually visible on screen, even if not explicitly hidden).
class DX11Renderer {
public:
    bool Init(HWND hwnd);
    void Shutdown();

    void OnResize(UINT width, UINT height);

    void BeginFrame();
    // Returns true if the swap chain reports the window is occluded — caller can
    // use this to further throttle work (already-rendered frame will be skipped).
    bool EndFrameAndPresent(bool vsync);

    ID3D11Device* Device() const { return device_; }
    ID3D11DeviceContext* Context() const { return context_; }

private:
    void CreateRenderTarget();
    void CleanupRenderTarget();

    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTargetView_ = nullptr;
    bool occluded_ = false;
};

} // namespace platform
