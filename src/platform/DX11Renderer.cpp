#include "DX11Renderer.h"
#include <d3d11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace platform {

bool DX11Renderer::Init(HWND hwnd) {
    hwnd_ = hwnd;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, featureLevelArray, 1,
        D3D11_SDK_VERSION, &desc, &swapChain_, &device_, &featureLevel, &context_);
    if (FAILED(hr)) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void DX11Renderer::Shutdown() {
    CleanupRenderTarget();
    if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void DX11Renderer::CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView_);
        backBuffer->Release();
    }
}

void DX11Renderer::CleanupRenderTarget() {
    if (renderTargetView_) {
        renderTargetView_->Release();
        renderTargetView_ = nullptr;
    }
}

void DX11Renderer::OnResize(UINT width, UINT height) {
    if (!swapChain_ || width == 0 || height == 0) {
        return;
    }
    CleanupRenderTarget();
    swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void DX11Renderer::BeginFrame() {
    const float clearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};
    context_->OMSetRenderTargets(1, &renderTargetView_, nullptr);
    context_->ClearRenderTargetView(renderTargetView_, clearColor);
}

bool DX11Renderer::EndFrameAndPresent(bool vsync) {
    HRESULT hr = swapChain_->Present(vsync ? 1 : 0, occluded_ ? DXGI_PRESENT_TEST : 0);
    occluded_ = (hr == DXGI_STATUS_OCCLUDED);
    return occluded_;
}

} // namespace platform
