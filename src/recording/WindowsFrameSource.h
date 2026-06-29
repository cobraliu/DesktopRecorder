#pragma once
#include "recording/FrameSource.h"
#include <cstdint>

namespace rr {

// Screen capture on Windows via GDI BitBlt from the screen DC into a top-down
// 32-bit DIB section, cropped to the requested region and converted to RGB24.
// Like X11FrameSource, this deliberately avoids FFmpeg's gdigrab/dshow input
// devices: the static/vcpkg FFmpeg build is configured with --disable-autodetect,
// so those indevs are not compiled in. GDI keeps the single-executable promise and
// is region-native. (DXGI Desktop Duplication is a possible later perf upgrade.)
class WindowsFrameSource : public FrameSource {
public:
    ~WindowsFrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    int width() const override { return width_; }
    int height() const override { return height_; }

private:
    // Opaque GDI handles (real types live in the .cpp to keep windows.h out of consumers).
    void* screenDC_ = nullptr;  // HDC for the whole virtual screen
    void* memDC_    = nullptr;  // HDC compatible memory DC
    void* bitmap_   = nullptr;  // HBITMAP DIB section selected into memDC_
    void* bits_     = nullptr;  // pointer to the DIB's BGRA pixels (top-down)

    int x_ = 0, y_ = 0;         // capture origin in virtual-screen coordinates
    int width_ = 0, height_ = 0;
    int fps_ = 0;

    // Frame pacing so the encoder (pts at 1/fps) sees real-time playback.
    long long startNs_ = 0;
    long long frameIndex_ = 0;
};

}  // namespace rr
