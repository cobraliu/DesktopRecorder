#pragma once
#include "recording/FrameSource.h"
#include <cstdint>

namespace rr {

// Screen capture on macOS via Quartz Display Services (CGDisplayCreateImageForRect),
// rendered into a tightly-packed RGBA bitmap context and converted to RGB24. This
// synchronous, region-native approach mirrors X11FrameSource (XShm) and
// WindowsFrameSource (GDI) and fits the FrameSource pull model directly. (The static
// FFmpeg build has no avfoundation indev, so we grab pixels ourselves.) ScreenCaptureKit
// (macOS 12.3+) is the modern asynchronous alternative and a possible later perf upgrade.
// Requires the Screen Recording TCC permission at runtime; without it the captured image
// is null and readFrame returns false.
class MacFrameSource : public FrameSource {
public:
    ~MacFrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    int width() const override { return width_; }
    int height() const override { return height_; }

private:
    unsigned int displayID_ = 0;  // CGDirectDisplayID (uint32_t)
    int x_ = 0, y_ = 0;           // capture origin in the display's local point space
    int width_ = 0, height_ = 0;
    int fps_ = 0;

    // Frame pacing so the encoder (pts at 1/fps) sees real-time playback.
    long long startNs_ = 0;
    long long frameIndex_ = 0;
};

}  // namespace rr
