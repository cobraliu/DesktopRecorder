#pragma once
#include "recording/FrameSource.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rr {

// Screen capture on macOS via ScreenCaptureKit (macOS 12.3+). SCStream pushes BGRA
// frames on its own dispatch queue; the newest frame is kept under a mutex and
// readFrame() — the Recorder's pull loop — copies it out paced to the target fps,
// the same push-to-pull bridge WaylandFrameSource uses for PipeWire.
//
// The stream's content filter excludes this application's own windows, so the
// floating Stop HUD, the red capture frame, and the countdown never appear in the
// recording (the macOS counterpart of Windows' WDA_EXCLUDEFROMCAPTURE).
//
// Requires the Screen Recording TCC permission; without it open() fails. The
// ObjC handles are stored as opaque pointers so this header stays plain C++ for
// the cross-platform factory.
class MacFrameSource : public FrameSource {
public:
    ~MacFrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    int width() const override { return width_; }
    int height() const override { return height_; }

    // Called from the SCK sample-handler queue / delegate; not part of the
    // FrameSource interface.
    void storeFrame(const uint8_t* bgra, int bufW, int bufH, int bytesPerRow);
    void markStreamDead() { streamDead_.store(true); }

private:
    void* stream_ = nullptr;   // SCStream*        (CFBridgingRetain'ed)
    void* handler_ = nullptr;  // RRStreamHandler* (stream output + delegate)
    void* queue_ = nullptr;    // dispatch_queue_t (serial sample-handler queue)

    int width_ = 0, height_ = 0;
    int fps_ = 0;

    std::mutex mutex_;
    std::vector<uint8_t> latest_;  // RGB24 top-down, stride = width_ * 3
    bool haveFrame_ = false;
    std::atomic<bool> streamDead_{false};

    // Frame pacing so the encoder (pts at 1/fps) sees real-time playback.
    long long startNs_ = 0;
    long long frameIndex_ = 0;
};

}  // namespace rr
