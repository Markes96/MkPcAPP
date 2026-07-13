#include "IconTextureCache.h"
#include "DX11Renderer.h"
#include "StringConvert.h"
#include "../startup/IconExtractor.h"
#include <windows.h>
#include <d3d11.h>
#include <optional>

namespace platform {

IconTextureCache::~IconTextureCache() {
    for (auto& [path, srv] : cache_) {
        if (srv) {
            srv->Release();
        }
    }
}

uint64_t IconTextureCache::GetOrCreateTexture(const std::string& resolvedExePath) {
    if (resolvedExePath.empty()) {
        return 0;
    }

    auto it = cache_.find(resolvedExePath);
    if (it != cache_.end()) {
        return it->second ? reinterpret_cast<uint64_t>(it->second) : 0;
    }

    std::optional<startup::IconBitmap> bitmap = startup::ExtractIconBitmap(Utf8ToWide(resolvedExePath));
    if (!bitmap.has_value()) {
        cache_[resolvedExePath] = nullptr;
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = static_cast<UINT>(bitmap->width);
    desc.Height = static_cast<UINT>(bitmap->height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = bitmap->rgbaPixels.data();
    initData.SysMemPitch = static_cast<UINT>(bitmap->width) * 4;

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = renderer_.Device()->CreateTexture2D(&desc, &initData, &texture);
    if (FAILED(hr) || !texture) {
        cache_[resolvedExePath] = nullptr;
        return 0;
    }

    ID3D11ShaderResourceView* srv = nullptr;
    hr = renderer_.Device()->CreateShaderResourceView(texture, nullptr, &srv);
    texture->Release(); // the SRV holds its own reference to the texture

    if (FAILED(hr) || !srv) {
        cache_[resolvedExePath] = nullptr;
        return 0;
    }

    cache_[resolvedExePath] = srv;
    return reinterpret_cast<uint64_t>(srv);
}

} // namespace platform
