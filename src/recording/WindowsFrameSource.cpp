#include "recording/WindowsFrameSource.h"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <thread>

namespace rr {

namespace {
long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

WindowsFrameSource::~WindowsFrameSource() { close(); }

bool WindowsFrameSource::open(const CaptureRegion& region, int fps) {
    // Virtual-screen bounds (covers all monitors); clamp so BitBlt stays in range.
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // The region arrives in Qt logical coordinates; GDI addresses physical
    // pixels, so map through the screen's device pixel ratio first.
    const double scale = region.dpiScale > 0 ? region.dpiScale : 1.0;
    const int rx = static_cast<int>(std::lround(region.x * scale));
    const int ry = static_cast<int>(std::lround(region.y * scale));
    int w = static_cast<int>(std::lround(region.w * scale));
    int h = static_cast<int>(std::lround(region.h * scale));
    // Clip, don't shift: a region hanging off the virtual screen's top/left
    // keeps only its on-screen part instead of capturing a displaced rectangle.
    if (rx < vx) w -= vx - rx;
    if (ry < vy) h -= vy - ry;
    x_ = rx < vx ? vx : rx;
    y_ = ry < vy ? vy : ry;
    if (x_ + w > vx + vw) w = vx + vw - x_;
    if (y_ + h > vy + vh) h = vy + vh - y_;
    if (w <= 0 || h <= 0) return false;
    // Encoders dislike odd dimensions; round down to even.
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) return false;
    width_ = w;
    height_ = h;
    fps_ = fps > 0 ? fps : 10;

    HDC screen = GetDC(nullptr);  // DC for the entire screen
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width_;
    bi.bmiHeader.biHeight = -height_;  // negative => top-down rows
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;      // BGRA (X byte unused)
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return false;
    }
    // Keep the displaced stock bitmap: DeleteObject silently fails on a bitmap
    // that is still selected into a DC, which would leak the DIB every recording.
    oldBitmap_ = SelectObject(mem, bmp);

    screenDC_ = screen;
    memDC_ = mem;
    bitmap_ = bmp;
    bits_ = bits;
    startNs_ = 0;
    frameIndex_ = 0;
    return true;
}

bool WindowsFrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    if (!memDC_) return false;

    // Pace to the target frame rate so the produced video matches wall-clock.
    const long long t = nowNs();
    if (startNs_ == 0) startNs_ = t;
    const long long deadline = startNs_ + frameIndex_ * (1000000000LL / fps_);
    if (t < deadline) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - t));
    }
    ++frameIndex_;

    HDC screen = static_cast<HDC>(screenDC_);
    HDC mem = static_cast<HDC>(memDC_);
    // CAPTUREBLT also grabs layered (e.g. transparent) windows.
    if (!BitBlt(mem, 0, 0, width_, height_, screen, x_, y_, SRCCOPY | CAPTUREBLT))
        return false;

    stride = width_ * 3;
    rgb.resize(static_cast<size_t>(stride) * height_);

    const int srcStride = width_ * 4;
    const auto* src = static_cast<const unsigned char*>(bits_);
    for (int row = 0; row < height_; ++row) {
        const unsigned char* s = src + static_cast<size_t>(row) * srcStride;
        unsigned char* d = rgb.data() + static_cast<size_t>(row) * stride;
        for (int col = 0; col < width_; ++col) {
            d[0] = s[2];  // R
            d[1] = s[1];  // G
            d[2] = s[0];  // B
            s += 4;
            d += 3;
        }
    }
    return true;
}

void WindowsFrameSource::close() {
    if (memDC_ && oldBitmap_) {
        // Deselect our DIB first; see the note at SelectObject in open().
        SelectObject(static_cast<HDC>(memDC_), static_cast<HGDIOBJ>(oldBitmap_));
        oldBitmap_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(static_cast<HBITMAP>(bitmap_));
        bitmap_ = nullptr;
    }
    if (memDC_) {
        DeleteDC(static_cast<HDC>(memDC_));
        memDC_ = nullptr;
    }
    if (screenDC_) {
        ReleaseDC(nullptr, static_cast<HDC>(screenDC_));
        screenDC_ = nullptr;
    }
    bits_ = nullptr;
}

}  // namespace rr
