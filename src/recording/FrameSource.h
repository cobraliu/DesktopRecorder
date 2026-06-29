#pragma once
#include <vector>
#include <cstdint>
#include "recording/types.h"

namespace rr {
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open(const CaptureRegion& region, int fps) = 0;
    virtual bool readFrame(std::vector<uint8_t>& rgb, int& stride) = 0;
    virtual void close() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
}
