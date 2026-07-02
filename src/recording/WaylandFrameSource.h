#pragma once
#include "recording/FrameSource.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rr {

// Screen capture for native Wayland sessions via xdg-desktop-portal's ScreenCast
// interface plus PipeWire. X11 grabbing (X11FrameSource) reads back black for
// native-Wayland windows under XWayland, so on Wayland we ask the compositor for
// a PipeWire video stream: the portal handshake (D-Bus) negotiates a session and
// the user consents through the compositor's own picker; the compositor then
// streams the chosen output/window over a PipeWire node.
//
// The portal yields a whole output/window, not an arbitrary rectangle, so the
// drag-selected CaptureRegion is cropped client-side within the captured source.
// Like X11FrameSource this deliberately avoids FFmpeg indevs (none are compiled
// into the static --disable-autodetect build) and produces top-down RGB24.
class WaylandFrameSource : public FrameSource {
public:
    ~WaylandFrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    // Aborts a portal handshake blocked in open() (the consent dialog can sit
    // unanswered for up to 60 s); called from the GUI thread via Recorder::stop.
    void requestStop() override;
    int width() const override { return width_; }
    int height() const override { return height_; }

    // Called from the PipeWire stream's process callback (its own thread). Public
    // so the file-static C trampoline in the .cpp can hand frames back to us.
    void onProcess();
    // Called from the PipeWire param-changed callback when the format is set.
    void onFormatChanged(int w, int h, int spaFormat);
    // Called from the PipeWire state-changed callback (values are pw_stream_state,
    // passed as int to keep PipeWire types out of this header).
    void onStreamStateChanged(int oldState, int newState);

private:
    // Opaque PipeWire handles (real types live in the .cpp to keep PipeWire/SPA
    // and QtDBus headers out of consumers of this header).
    void* threadLoop_ = nullptr;  // pw_thread_loop*
    void* context_    = nullptr;  // pw_context*
    void* core_       = nullptr;  // pw_core*
    void* stream_     = nullptr;  // pw_stream*
    // spa_hook listener storage (allocated in the .cpp where sizeof is known).
    void* streamHook_ = nullptr;  // spa_hook*
    int   pwFd_       = -1;       // PipeWire remote fd from OpenPipeWireRemote
    unsigned int nodeId_ = 0;     // ScreenCast stream node id

    // Portal session object path (for best-effort Close on teardown).
    void* sessionPath_ = nullptr;  // heap QString*

    // Source (whole captured output/window) geometry, from the Start results.
    int srcX_ = 0, srcY_ = 0, srcW_ = 0, srcH_ = 0;

    // Negotiated stream format.
    int fmtW_ = 0, fmtH_ = 0;
    int spaFormat_ = 0;  // spa_video_format enum value (BGRA/BGRx/RGBA/RGBx)

    // Latest decoded frame (cropped RGB24, width_*height_*3), guarded by mutex_.
    std::mutex mutex_;
    std::vector<uint8_t> latest_;
    bool haveFrame_ = false;

    // Set from the stream state callback when the compositor ends the cast
    // (e.g. the user clicks "stop sharing"); readFrame then fails instead of
    // silently recording the last frame forever.
    std::atomic<bool> streamDead_{false};

    // Set by requestStop(); polled by the portal-handshake wait loops in open().
    std::atomic<bool> stopRequested_{false};

    // Crop within the captured source and final (even) output dimensions.
    int cropX_ = 0, cropY_ = 0;
    int width_ = 0, height_ = 0;
    int fps_ = 0;

    // Frame pacing (mirrors X11FrameSource): reproduce real-time playback so the
    // encoder's 1/fps pts cadence matches wall-clock.
    long long startNs_ = 0;
    long long frameIndex_ = 0;
};

}  // namespace rr
