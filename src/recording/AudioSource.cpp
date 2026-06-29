#include "recording/AudioSource.h"

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace rr {

AudioSource::~AudioSource() { close(); }

bool AudioSource::open() {
    avdevice_register_all();
    // System ffmpeg usually ships with pulse; the vcpkg static build (AppImage) ships with alsa.
    // For both, "default" points to the desktop's default input device, so try them in order.
    const char* backends[] = { "pulse", "alsa" };
    const AVInputFormat* ifmt = nullptr;
    for (const char* name : backends) {
        ifmt = av_find_input_format(name);
        if (ifmt && avformat_open_input(&fmt_, "default", ifmt, nullptr) == 0)
            break;
        ifmt = nullptr;
        fmt_ = nullptr;   // on open failure, avformat_open_input has already freed and nulled it
    }
    if (!ifmt || !fmt_) return false;

    if (avformat_find_stream_info(fmt_, nullptr) < 0) return false;

    for (unsigned i = 0; i < fmt_->nb_streams; ++i) {
        if (fmt_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
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

    sampleRate_ = dec_->sample_rate;
    channels_ = dec_->ch_layout.nb_channels;
    if (sampleRate_ <= 0 || channels_ <= 0) return false;

    // Decoder native format -> S16 interleaved
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, channels_);
    if (swr_alloc_set_opts2(&swr_, &outLayout, AV_SAMPLE_FMT_S16, sampleRate_,
                            &dec_->ch_layout, dec_->sample_fmt, sampleRate_,
                            0, nullptr) < 0) {
        av_channel_layout_uninit(&outLayout);
        return false;
    }
    av_channel_layout_uninit(&outLayout);
    if (swr_init(swr_) < 0) return false;

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    return frame_ && pkt_;
}

bool AudioSource::readSamples(std::vector<int16_t>& interleaved, int& nbSamples) {
    while (av_read_frame(fmt_, pkt_) >= 0) {
        if (pkt_->stream_index != streamIdx_) { av_packet_unref(pkt_); continue; }
        const int s = avcodec_send_packet(dec_, pkt_);
        av_packet_unref(pkt_);
        if (s < 0) return false;
        const int r = avcodec_receive_frame(dec_, frame_);
        if (r == AVERROR(EAGAIN)) continue;
        if (r < 0) return false;

        nbSamples = frame_->nb_samples;
        interleaved.resize(static_cast<size_t>(nbSamples) * channels_);
        uint8_t* out[1] = { reinterpret_cast<uint8_t*>(interleaved.data()) };
        const int got = swr_convert(swr_, out, nbSamples,
                                    const_cast<const uint8_t**>(frame_->data),
                                    frame_->nb_samples);
        if (got < 0) return false;
        nbSamples = got;
        interleaved.resize(static_cast<size_t>(got) * channels_);
        return true;
    }
    return false;
}

void AudioSource::close() {
    if (swr_)   swr_free(&swr_);
    if (frame_) av_frame_free(&frame_);
    if (pkt_)   av_packet_free(&pkt_);
    if (dec_)   avcodec_free_context(&dec_);
    if (fmt_)   avformat_close_input(&fmt_);
    streamIdx_ = -1;
}

}
