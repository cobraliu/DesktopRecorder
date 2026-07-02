#pragma once
#include <string>
#include <cstdint>
#include <mutex>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct AVAudioFifo;
struct SwrContext;

namespace rr {

class Mp4Encoder {
public:
    Mp4Encoder() = default;
    ~Mp4Encoder();
    Mp4Encoder(const Mp4Encoder&) = delete;
    Mp4Encoder& operator=(const Mp4Encoder&) = delete;

    // Must be called before open() so the audio stream can be created before the file header is written
    void configureAudio(int sampleRate, int channels);

    bool open(int width, int height, int fps, const std::string& path);
    bool writeFrame(const uint8_t* rgb, int stride);
    // interleaved: S16 interleaved samples; nbSamples: samples per channel
    bool writeAudio(const int16_t* interleaved, int nbSamples);
    bool finish();

private:
    bool drainPackets();      // video
    bool drainAudio(bool flush);

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  codec_ = nullptr;
    AVStream*        stream_ = nullptr;
    AVFrame*         frame_ = nullptr;
    AVPacket*        pkt_ = nullptr;
    SwsContext*      sws_ = nullptr;
    int   width_ = 0;
    int   height_ = 0;
    int   fps_ = 0;
    // Video pts comes from the wall clock (see writeFrame), not a frame counter:
    // videoStartNs_ anchors t=0 at the first frame, lastPts_ enforces monotonicity.
    long long videoStartNs_ = -1;
    int64_t lastPts_ = -1;
    bool  opened_ = false;
    bool  finished_ = false;

    // Audio (optional)
    bool          audioConfigured_ = false;
    int           audioSampleRate_ = 0;
    int           audioChannels_ = 0;
    AVCodecContext* aCodec_ = nullptr;
    AVStream*       aStream_ = nullptr;
    AVFrame*        aFrame_ = nullptr;
    AVPacket*       aPkt_ = nullptr;
    AVAudioFifo*    fifo_ = nullptr;
    SwrContext*     swr_ = nullptr;
    int64_t         aPts_ = 0;

    // Guards writes to fmt_: the video and audio threads call writeFrame/writeAudio concurrently
    std::mutex      muxMutex_;
};

}
