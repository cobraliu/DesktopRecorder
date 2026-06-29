#include "recording/X11FrameSource.h"

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cstdlib>
#include <string>

namespace rr {

X11FrameSource::~X11FrameSource() { close(); }

bool X11FrameSource::open(const CaptureRegion& region, int fps) {
    avdevice_register_all();
    const AVInputFormat* ifmt = av_find_input_format("x11grab");
    if (!ifmt) return false;

    const char* disp = std::getenv("DISPLAY");
    std::string input = std::string(disp ? disp : ":0")
        + "+" + std::to_string(region.x) + "," + std::to_string(region.y);

    AVDictionary* opts = nullptr;
    const std::string vs = std::to_string(region.w) + "x" + std::to_string(region.h);
    av_dict_set(&opts, "video_size", vs.c_str(), 0);
    av_dict_set(&opts, "framerate", std::to_string(fps).c_str(), 0);

    if (avformat_open_input(&fmt_, input.c_str(), ifmt, &opts) < 0) {
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    if (avformat_find_stream_info(fmt_, nullptr) < 0) return false;

    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            streamIdx_ = static_cast<int>(i); break;
        }
    }
    if (streamIdx_ < 0) return false;

    AVCodecParameters* par = fmt_->streams[streamIdx_]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) return false;
    dec_ = avcodec_alloc_context3(dec);
    if (avcodec_parameters_to_context(dec_, par) < 0) return false;
    if (avcodec_open2(dec_, dec, nullptr) < 0) return false;

    width_ = par->width;
    height_ = par->height;

    frame_ = av_frame_alloc();
    rgbFrame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    sws_ = sws_getContext(width_, height_, (AVPixelFormat)par->format,
                          width_, height_, AV_PIX_FMT_RGB24,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    return sws_ != nullptr;
}

bool X11FrameSource::readFrame(std::vector<uint8_t>& rgb, int& stride) {
    while (av_read_frame(fmt_, pkt_) >= 0) {
        if (pkt_->stream_index != streamIdx_) { av_packet_unref(pkt_); continue; }
        const int s = avcodec_send_packet(dec_, pkt_);
        av_packet_unref(pkt_);
        if (s < 0) return false;
        const int r = avcodec_receive_frame(dec_, frame_);
        if (r == AVERROR(EAGAIN)) continue;
        if (r < 0) return false;

        stride = width_ * 3;
        rgb.resize(static_cast<size_t>(stride) * height_);
        uint8_t* dst[1] = { rgb.data() };
        int dstStride[1] = { stride };
        sws_scale(sws_, frame_->data, frame_->linesize, 0, height_, dst, dstStride);
        return true;
    }
    return false;
}

void X11FrameSource::close() {
    if (sws_)      { sws_freeContext(sws_); sws_ = nullptr; }
    if (frame_)    av_frame_free(&frame_);
    if (rgbFrame_) av_frame_free(&rgbFrame_);
    if (pkt_)      av_packet_free(&pkt_);
    if (dec_)      avcodec_free_context(&dec_);
    if (fmt_)      avformat_close_input(&fmt_);
    streamIdx_ = -1;
}

}
