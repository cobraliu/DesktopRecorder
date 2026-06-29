#include "recording/mp4_probe.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace rr::test {

Mp4Info probeMp4(const std::string& path) {
    Mp4Info info;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return info;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return info;
    }
    int vIdx = -1;
    int aIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const auto type = fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && vIdx < 0) {
            vIdx = static_cast<int>(i);
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            if (aIdx < 0) aIdx = static_cast<int>(i);
            info.audioStreams++;
            const AVCodec* ad = avcodec_find_decoder(fmt->streams[i]->codecpar->codec_id);
            if (info.audioCodec.empty()) info.audioCodec = ad ? ad->name : "";
        }
    }
    if (vIdx < 0) { avformat_close_input(&fmt); return info; }

    AVCodecParameters* par = fmt->streams[vIdx]->codecpar;
    info.width = par->width;
    info.height = par->height;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    info.codec = dec ? dec->name : "";

    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == vIdx) info.videoPackets++;
        else if (pkt->stream_index == aIdx) info.audioPackets++;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    info.ok = true;
    return info;
}

}
