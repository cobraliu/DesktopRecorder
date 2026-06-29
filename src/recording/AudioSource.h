#pragma once
#include <vector>
#include <cstdint>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;

namespace rr {

// Captures from the system default audio input (pulse) and outputs S16 interleaved samples.
class AudioSource {
public:
    ~AudioSource();
    AudioSource() = default;
    AudioSource(const AudioSource&) = delete;
    AudioSource& operator=(const AudioSource&) = delete;

    bool open();
    // interleaved: S16 interleaved; nbSamples: samples per channel. Returns false on end of stream/error.
    bool readSamples(std::vector<int16_t>& interleaved, int& nbSamples);
    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    void close();

private:
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  dec_ = nullptr;
    AVFrame*         frame_ = nullptr;
    AVPacket*        pkt_ = nullptr;
    SwrContext*      swr_ = nullptr;
    int streamIdx_ = -1;
    int sampleRate_ = 0;
    int channels_ = 0;
};

}
