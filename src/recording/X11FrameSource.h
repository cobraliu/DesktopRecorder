#pragma once
#include "recording/FrameSource.h"
#include <cstdint>

namespace rr {

// Screen capture using the X11 core protocol (XGetImage) with the MIT-SHM
// (XShm) fast path when available. This deliberately does NOT use FFmpeg's
// x11grab input device: the static/vcpkg FFmpeg build is configured with
// --disable-autodetect and the port exposes no xcb feature, so x11grab is
// never compiled into the self-contained binary. Grabbing pixels ourselves
// via libX11/libXext (already linked for hotkeys and window picking) keeps the
// single-executable promise intact and gives us RGB24 frames directly.
class X11FrameSource : public FrameSource {
public:
    ~X11FrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    int width() const override { return width_; }
    int height() const override { return height_; }

private:
    // Opaque X11 handles (real types live in the .cpp to keep Xlib headers out
    // of consumers of this header).
    void* dpy_   = nullptr;   // Display*
    void* image_ = nullptr;   // XImage*
    void* shm_   = nullptr;   // XShmSegmentInfo* (heap-allocated when useShm_)
    unsigned long root_ = 0;  // Window

    bool useShm_ = false;
    int  x_ = 0, y_ = 0;      // capture origin on the root window
    int  width_ = 0, height_ = 0;
    int  fps_ = 0;

    // Precomputed channel extraction from the server's pixel layout.
    int bitsPerPixel_ = 32;
    int redShift_ = 16, greenShift_ = 8, blueShift_ = 0;
    int redBits_ = 8, greenBits_ = 8, blueBits_ = 8;

    // Frame pacing: x11grab used to block until the next frame; we reproduce
    // that so the encoder (which stamps pts at 1/fps) sees real-time playback.
    long long startNs_ = 0;
    long long frameIndex_ = 0;
};

}
