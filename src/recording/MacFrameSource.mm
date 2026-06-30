#include "recording/MacFrameSource.h"

#include <CoreGraphics/CoreGraphics.h>

#include <chrono>
#include <thread>

namespace rr {

namespace {
long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

MacFrameSource::~MacFrameSource() { close(); }

bool MacFrameSource::open(const CaptureRegion& region, int fps) {
    // v1 captures the main display; multi-monitor display selection by screenIndex is a
    // runtime-verified follow-up. CGDisplayCreateImageForRect takes display-local points.
    displayID_ = CGMainDisplayID();
    const CGRect b = CGDisplayBounds(displayID_);  // global points, main display origin (0,0)
    const int dispW = static_cast<int>(b.size.width);
    const int dispH = static_cast<int>(b.size.height);

    // Convert the global capture origin to the display's local coordinate space and clamp.
    int lx = region.x - static_cast<int>(b.origin.x);
    int ly = region.y - static_cast<int>(b.origin.y);
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    int w = region.w;
    int h = region.h;
    if (lx + w > dispW) w = dispW - lx;
    if (ly + h > dispH) h = dispH - ly;
    if (w <= 0 || h <= 0) return false;
    // Encoders dislike odd dimensions; round down to even.
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0) return false;

    x_ = lx;
    y_ = ly;
    width_ = w;
    height_ = h;
    fps_ = fps > 0 ? fps : 10;
    startNs_ = 0;
    frameIndex_ = 0;
    return true;
}

bool MacFrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    if (width_ <= 0 || height_ <= 0) return false;

    // Pace to the target frame rate so the produced video matches wall-clock.
    const long long t = nowNs();
    if (startNs_ == 0) startNs_ = t;
    const long long deadline = startNs_ + frameIndex_ * (1000000000LL / fps_);
    if (t < deadline) {
        std::this_thread::sleep_for(std::chrono::nanoseconds(deadline - t));
    }
    ++frameIndex_;

    const CGRect rect = CGRectMake(x_, y_, width_, height_);
    // CGDisplayCreateImageForRect is deprecated on macOS 14+ (ScreenCaptureKit is the
    // replacement) but still functional with Screen Recording permission; silence the
    // deprecation so a newer SDK does not break the build.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CGImageRef img = CGDisplayCreateImageForRect(displayID_, rect);
#pragma clang diagnostic pop
    if (!img) return false;

    // Render into a tightly-packed RGBA buffer sized exactly width_ x height_. On Retina
    // the captured image has more pixels than requested points; drawing into a fixed-size
    // context scales it down so the dimensions stay consistent with width()/height().
    std::vector<uint8_t> rgba(static_cast<size_t>(width_) * height_ * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        rgba.data(), static_cast<size_t>(width_), static_cast<size_t>(height_), 8,
        static_cast<size_t>(width_) * 4, cs,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);  // bytes: R,G,B,A
    CGColorSpaceRelease(cs);
    if (!ctx) {
        CGImageRelease(img);
        return false;
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, width_, height_), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    // CGBitmapContext has a bottom-left origin, so row 0 is the bottom scanline; flip
    // vertically while converting RGBA -> RGB24 to produce top-down rows for the encoder.
    stride = width_ * 3;
    rgb.resize(static_cast<size_t>(stride) * height_);
    for (int row = 0; row < height_; ++row) {
        const uint8_t* s = rgba.data() + static_cast<size_t>(height_ - 1 - row) * width_ * 4;
        uint8_t* d = rgb.data() + static_cast<size_t>(row) * stride;
        for (int col = 0; col < width_; ++col) {
            d[0] = s[0];  // R
            d[1] = s[1];  // G
            d[2] = s[2];  // B
            s += 4;
            d += 3;
        }
    }
    return true;
}

void MacFrameSource::close() {
    width_ = 0;
    height_ = 0;
}

}  // namespace rr
