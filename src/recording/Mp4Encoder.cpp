#include "recording/Mp4Encoder.h"

#include <chrono>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace rr {

Mp4Encoder::~Mp4Encoder() {
    // Do not auto-write the trailer: if interrupted, it stays interrupted; relying on fragmented chunks the file is still playable
    if (sws_)    sws_freeContext(sws_);
    if (frame_)  av_frame_free(&frame_);
    if (pkt_)    av_packet_free(&pkt_);
    if (codec_)  avcodec_free_context(&codec_);
    // Audio resources
    if (swr_)    swr_free(&swr_);
    if (fifo_)   { av_audio_fifo_free(fifo_); fifo_ = nullptr; }
    if (aFrame_) av_frame_free(&aFrame_);
    if (aPkt_)   av_packet_free(&aPkt_);
    if (aCodec_) avcodec_free_context(&aCodec_);
    if (fmt_) {
        if (fmt_->pb) avio_closep(&fmt_->pb);
        avformat_free_context(fmt_);
        fmt_ = nullptr;
    }
}

void Mp4Encoder::configureAudio(int sampleRate, int channels) {
    audioSampleRate_ = sampleRate;
    audioChannels_ = channels;
    audioConfigured_ = true;
}

bool Mp4Encoder::open(int width, int height, int fps, const std::string& path) {
    width_ = width; height_ = height; fps_ = fps;

    if (avformat_alloc_output_context2(&fmt_, nullptr, "mp4", path.c_str()) < 0 || !fmt_)
        return false;

    const AVCodec* enc = avcodec_find_encoder_by_name("libx264");
    if (!enc) return false;

    stream_ = avformat_new_stream(fmt_, nullptr);
    if (!stream_) return false;

    codec_ = avcodec_alloc_context3(enc);
    if (!codec_) return false;
    codec_->width = width;
    codec_->height = height;
    codec_->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_->time_base = AVRational{1, fps};
    codec_->framerate = AVRational{fps, 1};
    codec_->gop_size = fps; // one keyframe per second, helps fragmented playback
    if (fmt_->oformat->flags & AVFMT_GLOBALHEADER)
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(codec_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codec_, enc, nullptr) < 0) return false;
    if (avcodec_parameters_from_context(stream_->codecpar, codec_) < 0) return false;
    stream_->time_base = codec_->time_base;

    // Optional audio stream: must be created before avformat_write_header
    if (audioConfigured_) {
        const AVCodec* aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!aenc) return false;
        aStream_ = avformat_new_stream(fmt_, nullptr);
        if (!aStream_) return false;
        aCodec_ = avcodec_alloc_context3(aenc);
        if (!aCodec_) return false;
        aCodec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aCodec_->sample_rate = audioSampleRate_;
        aCodec_->bit_rate = 128000;
        av_channel_layout_default(&aCodec_->ch_layout, audioChannels_);
        aCodec_->time_base = AVRational{1, audioSampleRate_};
        if (fmt_->oformat->flags & AVFMT_GLOBALHEADER)
            aCodec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(aCodec_, aenc, nullptr) < 0) return false;
        if (avcodec_parameters_from_context(aStream_->codecpar, aCodec_) < 0) return false;
        aStream_->time_base = aCodec_->time_base;

        // FIFO buffers up to the encoder frame_size; resample S16 interleaved -> FLTP planar
        fifo_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, audioChannels_, 1);
        if (!fifo_) return false;
        AVChannelLayout inLayout;
        av_channel_layout_default(&inLayout, audioChannels_);
        if (swr_alloc_set_opts2(&swr_, &aCodec_->ch_layout, AV_SAMPLE_FMT_FLTP, audioSampleRate_,
                                &inLayout, AV_SAMPLE_FMT_S16, audioSampleRate_, 0, nullptr) < 0) {
            av_channel_layout_uninit(&inLayout);
            return false;
        }
        av_channel_layout_uninit(&inLayout);
        if (swr_init(swr_) < 0) return false;

        aFrame_ = av_frame_alloc();
        aFrame_->nb_samples = aCodec_->frame_size;
        aFrame_->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&aFrame_->ch_layout, &aCodec_->ch_layout);
        aFrame_->sample_rate = audioSampleRate_;
        if (av_frame_get_buffer(aFrame_, 0) < 0) return false;

        aPkt_ = av_packet_alloc();
    }

    if (avio_open(&fmt_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    if (avformat_write_header(fmt_, &opts) < 0) { av_dict_free(&opts); return false; }
    av_dict_free(&opts);

    frame_ = av_frame_alloc();
    frame_->format = AV_PIX_FMT_YUV420P;
    frame_->width = width;
    frame_->height = height;
    if (av_frame_get_buffer(frame_, 0) < 0) return false;

    pkt_ = av_packet_alloc();
    sws_ = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                          width, height, AV_PIX_FMT_YUV420P,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) return false;

    opened_ = true;
    return true;
}

bool Mp4Encoder::drainPackets() {
    for (;;) {
        const int ret = avcodec_receive_packet(codec_, pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) return false;
        av_packet_rescale_ts(pkt_, codec_->time_base, stream_->time_base);
        pkt_->stream_index = stream_->index;
        const int w = av_interleaved_write_frame(fmt_, pkt_);
        av_packet_unref(pkt_);
        if (w < 0) return false;
    }
}

bool Mp4Encoder::drainAudio(bool /*flush*/) {
    for (;;) {
        const int ret = avcodec_receive_packet(aCodec_, aPkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return true;
        if (ret < 0) return false;
        av_packet_rescale_ts(aPkt_, aCodec_->time_base, aStream_->time_base);
        aPkt_->stream_index = aStream_->index;
        const int w = av_interleaved_write_frame(fmt_, aPkt_);
        av_packet_unref(aPkt_);
        if (w < 0) return false;
    }
}

bool Mp4Encoder::writeFrame(const uint8_t* rgb, int stride) {
    if (!opened_) return false;
    std::lock_guard<std::mutex> lk(muxMutex_);
    if (av_frame_make_writable(frame_) < 0) return false;

    const uint8_t* src[1] = { rgb };
    const int srcStride[1] = { stride };
    sws_scale(sws_, src, srcStride, 0, height_,
              frame_->data, frame_->linesize);

    // Stamp the frame with wall-clock elapsed time in 1/fps ticks rather than a frame
    // counter: capture regularly runs slower than the target fps, and counter timestamps
    // would compress the timeline and drift away from the sample-clocked audio track.
    const long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (videoStartNs_ < 0) videoStartNs_ = nowNs;
    int64_t pts = static_cast<int64_t>(
        llround(static_cast<double>(nowNs - videoStartNs_) * fps_ / 1e9));
    if (pts <= lastPts_) pts = lastPts_ + 1;   // libx264 requires strictly increasing pts
    lastPts_ = pts;

    frame_->pts = pts;
    if (avcodec_send_frame(codec_, frame_) < 0) return false;
    return drainPackets();
}

bool Mp4Encoder::writeAudio(const int16_t* interleaved, int nbSamples) {
    if (!opened_ || !audioConfigured_) return false;
    std::lock_guard<std::mutex> lk(muxMutex_);

    // S16 interleaved -> FLTP planar, write into FIFO
    uint8_t** conv = nullptr;
    if (av_samples_alloc_array_and_samples(&conv, nullptr, audioChannels_,
                                           nbSamples, AV_SAMPLE_FMT_FLTP, 0) < 0)
        return false;
    const uint8_t* in[1] = { reinterpret_cast<const uint8_t*>(interleaved) };
    const int got = swr_convert(swr_, conv, nbSamples, in, nbSamples);
    bool ok = got >= 0;
    if (ok && got > 0)
        ok = av_audio_fifo_write(fifo_, reinterpret_cast<void**>(conv), got) >= got;
    if (conv) { av_freep(&conv[0]); av_freep(&conv); }
    if (!ok) return false;

    // Once a full frame (frame_size) has accumulated, encode it
    while (av_audio_fifo_size(fifo_) >= aCodec_->frame_size) {
        if (av_frame_make_writable(aFrame_) < 0) return false;
        if (av_audio_fifo_read(fifo_, reinterpret_cast<void**>(aFrame_->data),
                               aCodec_->frame_size) < aCodec_->frame_size)
            return false;
        aFrame_->pts = aPts_;
        aPts_ += aCodec_->frame_size;
        if (avcodec_send_frame(aCodec_, aFrame_) < 0) return false;
        if (!drainAudio(false)) return false;
    }
    return true;
}

bool Mp4Encoder::finish() {
    if (!opened_ || finished_) return false;
    std::lock_guard<std::mutex> lk(muxMutex_);
    finished_ = true;
    // flush video
    if (avcodec_send_frame(codec_, nullptr) < 0) return false;
    if (!drainPackets()) return false;
    // flush audio (remaining samples shorter than one frame are ignored, roughly <23ms)
    if (audioConfigured_ && aCodec_) {
        if (avcodec_send_frame(aCodec_, nullptr) < 0) return false;
        if (!drainAudio(true)) return false;
    }
    if (av_write_trailer(fmt_) < 0) return false;
    if (fmt_->pb) avio_closep(&fmt_->pb);
    return true;
}

}
