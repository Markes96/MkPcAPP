#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace startup {

// A top-down, 32-bit RGBA bitmap (no premultiplied alpha), ready to upload
// as-is to a DXGI_FORMAT_R8G8B8A8_UNORM texture.
struct IconBitmap {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgbaPixels; // width * height * 4 bytes
};

// Pure Win32/GDI, no D3D/ImGui knowledge -- kept separate from texture
// upload so this half is reviewable/reasoned-about independent of rendering.
// Returns nullopt if the exe has no icon, doesn't exist, or extraction fails
// for any other reason -- callers fall back to a placeholder glyph.
std::optional<IconBitmap> ExtractIconBitmap(const std::wstring& exePath);

} // namespace startup
