#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include "recording/types.h"

namespace rr {
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open(const CaptureRegion& region, int fps) = 0;
    virtual bool readFrame(std::vector<uint8_t>& rgb, int& stride) = 0;
    virtual void close() = 0;
    // Best-effort asynchronous cancel, callable from another thread while open()
    // or readFrame() is in flight. Backends whose open() can block for a long
    // time (the Wayland portal consent dialog) override this; for the fast
    // native grabbers the default no-op is fine.
    virtual void requestStop() {}
    virtual int width() const = 0;
    virtual int height() const = 0;
};

// Creates the screen-capture backend for the current platform/session.
// Returns nullptr on platforms without a backend yet (callers must null-check).
std::unique_ptr<FrameSource> createFrameSource();
}
