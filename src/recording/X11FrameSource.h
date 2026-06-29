#pragma once
#include "recording/FrameSource.h"
#include <string>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace rr {
class X11FrameSource : public FrameSource {
public:
    ~X11FrameSource() override;
    bool open(const CaptureRegion& region, int fps) override;
    bool readFrame(std::vector<uint8_t>& rgb, int& stride) override;
    void close() override;
    int width() const override { return width_; }
    int height() const override { return height_; }
private:
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  dec_ = nullptr;
    AVFrame*         frame_ = nullptr;
    AVFrame*         rgbFrame_ = nullptr;
    AVPacket*        pkt_ = nullptr;
    SwsContext*      sws_ = nullptr;
    int streamIdx_ = -1;
    int width_ = 0;
    int height_ = 0;
};
}
