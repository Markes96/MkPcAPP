#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

struct ID3D11ShaderResourceView;

namespace platform {

class DX11Renderer;

// Uploads icon bitmaps (from startup::IconExtractor) to immutable D3D11
// textures and caches the result per resolved exe path, so a startup-entry
// card doesn't re-extract/re-upload every frame. Deliberately not folded
// into DX11Renderer itself, so the generic renderer stays free of any one
// feature's state -- StartupTab is the only owner today, but any future tab
// needing ImGui::Image() could reuse this the same way.
//
// Must only be called from the thread that owns the D3D11 device/context
// (the render thread, i.e. from ITab::OnRender) -- never from a background
// tick thread.
class IconTextureCache {
public:
    explicit IconTextureCache(DX11Renderer& renderer) : renderer_(renderer) {}
    ~IconTextureCache();

    IconTextureCache(const IconTextureCache&) = delete;
    IconTextureCache& operator=(const IconTextureCache&) = delete;

    // Returns 0 if extraction/upload failed for this path -- callers should
    // render a placeholder glyph instead of ImGui::Image() in that case. A
    // failure is cached too (as a null entry), so it's never retried every
    // frame. The returned value is a raw ImTextureID (ImU64 in this ImGui
    // version) -- an ID3D11ShaderResourceView* reinterpreted, matching what
    // imgui_impl_dx11's own texture path expects.
    uint64_t GetOrCreateTexture(const std::string& resolvedExePath);

private:
    DX11Renderer& renderer_;
    std::unordered_map<std::string, ID3D11ShaderResourceView*> cache_; // nullptr = extraction/upload failed
};

} // namespace platform
