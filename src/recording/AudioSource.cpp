#include "recording/AudioSource.h"

#if !defined(_WIN32) && !defined(__APPLE__)

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

}  // namespace rr

#elif defined(_WIN32)  // WASAPI shared-mode capture of the default input device

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

#include <cmath>

namespace rr {

namespace {

// REFERENCE_TIME is in 100-ns units; request a 200 ms capture buffer.
constexpr long long kBufferDurationHns = 2000000;

template <typename T>
void safeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

// Identify the sample format of a WASAPI shared mix format. For WAVE_FORMAT_EXTENSIBLE,
// the SubFormat GUID's Data1 equals the legacy WAVE_FORMAT_* tag, so we avoid ksmedia.h.
enum class SampleFmt { Unknown, Flt, S16, S32 };

SampleFmt classifyFormat(const WAVEFORMATEX* wf) {
    WORD tag = wf->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        tag = static_cast<WORD>(ext->SubFormat.Data1);
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT) return SampleFmt::Flt;
    if (tag == WAVE_FORMAT_PCM) {
        if (wf->wBitsPerSample == 16) return SampleFmt::S16;
        if (wf->wBitsPerSample == 32) return SampleFmt::S32;
    }
    return SampleFmt::Unknown;
}

int16_t clampToS16(double v) {
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<int16_t>(std::lround(v));
}

}  // namespace

AudioSource::~AudioSource() { close(); }

bool AudioSource::open() {
    running_.store(true);
    ready_ = false;
    openOk_ = false;
    captureThread_ = std::thread([this] { captureLoop(); });

    // Wait for the capture thread to negotiate the format (or fail).
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return ready_; });
    const bool ok = openOk_;
    lock.unlock();

    if (!ok) {
        // Negotiation failed; the thread has already returned. Join and reset.
        running_.store(false);
        if (captureThread_.joinable()) captureThread_.join();
    }
    return ok;
}

void AudioSource::captureLoop() {
    const bool comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* wf = nullptr;
    SampleFmt fmt = SampleFmt::Unknown;
    int bytesPerSample = 0;

    auto fail = [&]() {
        if (wf) CoTaskMemFree(wf);
        safeRelease(capture);
        safeRelease(client);
        safeRelease(device);
        safeRelease(enumerator);
        if (comInit) CoUninitialize();
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = true;
        openOk_ = false;
        cv_.notify_all();
    };

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator)))) { fail(); return; }
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) { fail(); return; }
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(&client)))) { fail(); return; }
    if (FAILED(client->GetMixFormat(&wf)) || !wf) { fail(); return; }

    fmt = classifyFormat(wf);
    if (fmt == SampleFmt::Unknown) { fail(); return; }
    bytesPerSample = wf->wBitsPerSample / 8;

    if (FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                  kBufferDurationHns, 0, wf, nullptr))) { fail(); return; }
    if (FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                  reinterpret_cast<void**>(&capture)))) { fail(); return; }
    if (FAILED(client->Start())) { fail(); return; }

    const int channels = wf->nChannels;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sampleRate_ = static_cast<int>(wf->nSamplesPerSec);
        channels_ = channels;
        ready_ = true;
        openOk_ = true;
        cv_.notify_all();
    }

    // Pull packets, convert to S16, and append to the queue until close() stops us.
    while (running_.load()) {
        UINT32 packetFrames = 0;
        if (FAILED(capture->GetNextPacketSize(&packetFrames))) break;
        if (packetFrames == 0) { Sleep(5); continue; }

        while (packetFrames != 0) {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr))) {
                packetFrames = 0;
                break;
            }
            const size_t count = static_cast<size_t>(numFrames) * channels;
            std::vector<int16_t> chunk(count);
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if (silent || data == nullptr) {
                // chunk already zero-initialized
            } else if (fmt == SampleFmt::Flt) {
                const auto* s = reinterpret_cast<const float*>(data);
                for (size_t i = 0; i < count; ++i) chunk[i] = clampToS16(s[i] * 32767.0);
            } else if (fmt == SampleFmt::S16) {
                const auto* s = reinterpret_cast<const int16_t*>(data);
                for (size_t i = 0; i < count; ++i) chunk[i] = s[i];
            } else {  // S32
                const auto* s = reinterpret_cast<const int32_t*>(data);
                for (size_t i = 0; i < count; ++i) chunk[i] = static_cast<int16_t>(s[i] >> 16);
            }
            (void)bytesPerSample;
            capture->ReleaseBuffer(numFrames);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.insert(queue_.end(), chunk.begin(), chunk.end());
            }
            cv_.notify_all();

            if (FAILED(capture->GetNextPacketSize(&packetFrames))) { packetFrames = 0; break; }
        }
    }

    client->Stop();
    CoTaskMemFree(wf);
    safeRelease(capture);
    safeRelease(client);
    safeRelease(device);
    safeRelease(enumerator);
    if (comInit) CoUninitialize();

    // Wake any reader blocked waiting for data so it can observe end-of-stream.
    cv_.notify_all();
}

bool AudioSource::readSamples(std::vector<int16_t>& interleaved, int& nbSamples) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
    if (queue_.empty()) {
        nbSamples = 0;
        return false;  // capture stopped and drained
    }
    interleaved.assign(queue_.begin(), queue_.end());
    queue_.clear();
    lock.unlock();

    nbSamples = channels_ > 0 ? static_cast<int>(interleaved.size()) / channels_ : 0;
    return nbSamples > 0;
}

void AudioSource::close() {
    running_.store(false);
    cv_.notify_all();
    if (captureThread_.joinable()) captureThread_.join();
    queue_.clear();
}

}  // namespace rr

#else  // __APPLE__ — CoreAudio AudioQueue capture of the default input device

#include <AudioToolbox/AudioQueue.h>

namespace rr {

namespace {

// AudioQueue input callback. Runs on the queue's internal thread: copy the S16 samples
// into the AudioSource queue, then re-enqueue the buffer for reuse.
void rrAudioInputCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef buf,
                          const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    if (auto* self = static_cast<AudioSource*>(userData); self && buf) {
        self->appendCaptured(static_cast<const int16_t*>(buf->mAudioData),
                             buf->mAudioDataByteSize / sizeof(int16_t));
    }
    if (inAQ && buf) AudioQueueEnqueueBuffer(inAQ, buf, 0, nullptr);
}

}  // namespace

AudioSource::~AudioSource() { close(); }

bool AudioSource::open() {
    // Request S16 interleaved 48 kHz stereo directly; AudioQueue converts from the device's
    // native format. (Exact mono-mic / sample-rate behaviour is runtime-verified on a Mac.)
    AudioStreamBasicDescription asbd = {};
    asbd.mSampleRate = 48000.0;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 16;
    asbd.mBytesPerFrame = asbd.mChannelsPerFrame * static_cast<UInt32>(sizeof(int16_t));
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;

    AudioQueueRef q = nullptr;
    if (AudioQueueNewInput(&asbd, &rrAudioInputCallback, this, nullptr, nullptr, 0, &q) != noErr)
        return false;
    queue_ = q;
    sampleRate_ = 48000;
    channels_ = 2;

    // Four ~100 ms buffers in rotation.
    const UInt32 bytesPerBuffer = asbd.mBytesPerFrame * 4800;
    for (int i = 0; i < 4; ++i) {
        AudioQueueBufferRef buf = nullptr;
        if (AudioQueueAllocateBuffer(q, bytesPerBuffer, &buf) != noErr) { close(); return false; }
        if (AudioQueueEnqueueBuffer(q, buf, 0, nullptr) != noErr) { close(); return false; }
    }

    running_.store(true);
    if (AudioQueueStart(q, nullptr) != noErr) { close(); return false; }
    return true;
}

void AudioSource::appendCaptured(const int16_t* data, std::size_t count) {
    if (!running_.load() || count == 0 || !data) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.insert(samples_.end(), data, data + count);
    }
    cv_.notify_all();
}

bool AudioSource::readSamples(std::vector<int16_t>& interleaved, int& nbSamples) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !samples_.empty() || !running_.load(); });
    if (samples_.empty()) {
        nbSamples = 0;
        return false;  // capture stopped and drained
    }
    interleaved.assign(samples_.begin(), samples_.end());
    samples_.clear();
    lock.unlock();

    nbSamples = channels_ > 0 ? static_cast<int>(interleaved.size()) / channels_ : 0;
    return nbSamples > 0;
}

void AudioSource::close() {
    running_.store(false);
    cv_.notify_all();
    if (queue_) {
        AudioQueueStop(static_cast<AudioQueueRef>(queue_), true);
        AudioQueueDispose(static_cast<AudioQueueRef>(queue_), true);
        queue_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
}

}  // namespace rr

#endif  // platform audio backend
