#include "IconExtractor.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <utility>

namespace startup {

namespace {

std::optional<IconBitmap> BitmapFromIcon(HICON hIcon) {
    if (!hIcon) {
        return std::nullopt;
    }

    ICONINFO iconInfo = {};
    if (!GetIconInfo(hIcon, &iconInfo)) {
        return std::nullopt;
    }

    BITMAP bmp = {};
    if (!GetObject(iconInfo.hbmColor, sizeof(bmp), &bmp)) {
        if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
        if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);
        return std::nullopt;
    }

    int width = bmp.bmWidth;
    int height = bmp.bmHeight;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // negative = top-down, avoids a manual row flip
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    bool ok = false;
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        ok = GetDIBits(hdc, iconInfo.hbmColor, 0, height, pixels.data(), &bmi, DIB_RGB_COLORS) != 0;
        ReleaseDC(nullptr, hdc);
    }

    if (iconInfo.hbmColor) DeleteObject(iconInfo.hbmColor);
    if (iconInfo.hbmMask) DeleteObject(iconInfo.hbmMask);

    if (!ok) {
        return std::nullopt;
    }

    // GDI returns BGRA -- swap to RGBA so every downstream consumer
    // (IconTextureCache -> D3D11) only ever deals with one fixed layout.
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        std::swap(pixels[i], pixels[i + 2]);
    }

    IconBitmap result;
    result.width = width;
    result.height = height;
    result.rgbaPixels = std::move(pixels);
    return result;
}

} // namespace

std::optional<IconBitmap> ExtractIconBitmap(const std::wstring& exePath) {
    if (exePath.empty()) {
        return std::nullopt;
    }

    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    UINT extracted = ExtractIconExW(exePath.c_str(), 0, &largeIcon, &smallIcon, 1);

    HICON iconToUse = nullptr;
    bool iconFromShellFallback = false;
    if (extracted > 0 && largeIcon) {
        iconToUse = largeIcon;
    } else {
        // Fallback: some exes (certain icon-less or shell-registered
        // binaries) yield nothing via ExtractIconEx but still have an
        // associated shell icon.
        SHFILEINFOW shfi = {};
        if (SHGetFileInfoW(exePath.c_str(), 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_LARGEICON)) {
            iconToUse = shfi.hIcon;
            iconFromShellFallback = true;
        }
    }

    if (!iconToUse) {
        if (largeIcon) DestroyIcon(largeIcon);
        if (smallIcon) DestroyIcon(smallIcon);
        return std::nullopt;
    }

    std::optional<IconBitmap> result = BitmapFromIcon(iconToUse);

    if (iconFromShellFallback) {
        DestroyIcon(iconToUse);
    }
    if (largeIcon) DestroyIcon(largeIcon);
    if (smallIcon) DestroyIcon(smallIcon);

    return result;
}

} // namespace startup
