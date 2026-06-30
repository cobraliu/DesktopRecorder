#pragma once
#include <vector>
#include <cstdint>

#if defined(_WIN32)
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#else
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
#endif

namespace rr {

// Captures from the system default audio input and outputs S16 interleaved samples.
// Linux uses libavdevice (pulse/alsa); Windows uses WASAPI shared-mode capture.
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
    int sampleRate_ = 0;
    int channels_ = 0;

#if defined(_WIN32)
    // open() and readSamples() are called from different threads, and WASAPI/COM
    // objects are apartment-bound. To keep COM confined to one thread, a dedicated
    // capture thread owns every COM object, pulls packets, converts them to S16,
    // and appends to queue_; readSamples() (on the consumer thread) only drains it.
    void captureLoop();   // runs on captureThread_, owns all COM state
    std::thread captureThread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<int16_t> queue_;  // interleaved S16, guarded by mutex_
    bool ready_ = false;         // capture thread finished negotiating the format
    bool openOk_ = false;        // negotiation succeeded
#else
    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  dec_ = nullptr;
    AVFrame*         frame_ = nullptr;
    AVPacket*        pkt_ = nullptr;
    SwrContext*      swr_ = nullptr;
    int streamIdx_ = -1;
#endif
};

}
