# RegionRecord Audio Recording Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps are tracked with checkboxes.

**Goal:** Make the "record audio" toggle actually work -- during recording, encode the system default audio input as AAC and multiplex it together with the H.264 video into the same fragmented MP4.

**Architecture:** Three steps. (1) `Mp4Encoder` adds an optional AAC audio stream (S16 interleaved input -> FIFO buffered to 1024 frames -> swr converts to FLTP -> encode -> `av_interleaved_write_frame`), with an internal `std::mutex` so the video thread and audio thread can write concurrently. (2) Add `AudioSource` (libavdevice `pulse`, input `default`, reads out S16 interleaved samples). (3) When `audioEnabled`, `Recorder` spins up an additional audio thread that feeds the same `Mp4Encoder` concurrently with the video loop.

**Tech Stack:** libavcodec AAC, libavdevice pulse, libswresample, libavutil AVAudioFifo, std::thread/std::mutex, QtTest.

## Global Constraints

- Do not break the single static binary: only use the already-linked libav* and the system pulse (carried in the AppImage).
- Do not introduce new third-party libraries.
- Audio is off by default; enabled only when `OutputOptions::audioEnabled==true`.
- The existing `Mp4Encoder::open(w,h,fps,path)` signature **stays unchanged** (behavior is identical when there is no audio); audio is configured via the new `configureAudio()` before open().
- Input sample format convention: S16 interleaved (`int16_t`), uniformly converted and provided by `AudioSource`.
- A/V sync strategy (v1): both tracks start their PTS from 0 at recording start, relying on `av_interleaved_write_frame` to interleave by DTS; no fine drift correction.

---

### Task 1: Mp4Encoder adds an AAC audio stream (synthetic-sample TDD)

**Files:**
- Modify: `src/recording/Mp4Encoder.h`, `src/recording/Mp4Encoder.cpp`
- Modify: `tests/recording/mp4_probe.h`, `tests/recording/mp4_probe.cpp` (probe adds audio info)
- Modify: `tests/recording/test_mp4_encoder.cpp`

**Interfaces:**
- Add `void Mp4Encoder::configureAudio(int sampleRate, int channels);` (must be called before `open()`)
- Add `bool Mp4Encoder::writeAudio(const int16_t* interleaved, int nbSamples);`
- Inside `open()`: if `configureAudio` was called, create the AAC stream before `avformat_write_header`.
- `finish()`: first flush video, then flush audio, then write the trailer.
- probe extension: `Mp4Info` adds `int audioStreams; int audioPackets; std::string audioCodec;`

- [ ] **Step 1: Extend the probe (so the test can assert on audio first)**

Add fields to `Mp4Info` in `tests/recording/mp4_probe.h`:
```cpp
struct Mp4Info {
    bool ok = false;
    int width = 0;
    int height = 0;
    int videoPackets = 0;
    std::string codec;
    int audioStreams = 0;
    int audioPackets = 0;
    std::string audioCodec;
};
```
`tests/recording/mp4_probe.cpp`: while iterating streams, count audio streams and their codec name; in the packet-reading loop, count audio-stream packets:
```cpp
    int aIdx = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const auto type = fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && vIdx < 0) vIdx = int(i);
        else if (type == AVMEDIA_TYPE_AUDIO) {
            aIdx = int(i);
            info.audioStreams++;
            const AVCodec* ad = avcodec_find_decoder(fmt->streams[i]->codecpar->codec_id);
            info.audioCodec = ad ? ad->name : "";
        }
    }
```
In the packet-reading loop:
```cpp
        if (pkt->stream_index == vIdx) info.videoPackets++;
        else if (pkt->stream_index == aIdx) info.audioPackets++;
```
> Note: keep the original vIdx selection logic (first video stream) and merge the above into it; the `vIdx < 0` check ensures only the first video stream is recognized.

- [ ] **Step 2: Write the failing test**

Add to `tests/recording/test_mp4_encoder.cpp`:
```cpp
void TestMp4Encoder::encodesVideoPlusAudio() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = (dir.path() + "/av.mp4").toStdString();

    const int W = 320, H = 240, FPS = 30, N = 30;
    const int SR = 44100, CH = 2;
    rr::Mp4Encoder enc;
    enc.configureAudio(SR, CH);
    QVERIFY(enc.open(W, H, FPS, path));

    std::vector<uint8_t> rgb(size_t(W) * H * 3, 0);
    // 1/30 second per frame -> corresponding number of audio samples
    const int samplesPerFrame = SR / FPS;
    std::vector<int16_t> pcm(size_t(samplesPerFrame) * CH);
    for (int f = 0; f < N; ++f) {
        std::fill(rgb.begin(), rgb.end(), uint8_t(f * 8));
        QVERIFY(enc.writeFrame(rgb.data(), W * 3));
        // synthesize a sine/sawtooth segment to ensure it is not all zeros
        for (int s = 0; s < samplesPerFrame; ++s) {
            const int16_t v = int16_t((s + f * 131) % 2000 - 1000);
            pcm[size_t(s) * CH] = v;
            pcm[size_t(s) * CH + 1] = v;
        }
        QVERIFY(enc.writeAudio(pcm.data(), samplesPerFrame));
    }
    QVERIFY(enc.finish());

    const rr::test::Mp4Info info = rr::test::probeMp4(path);
    QVERIFY(info.ok);
    QCOMPARE(info.codec, std::string("h264"));
    QCOMPARE(info.audioStreams, 1);
    QCOMPARE(info.audioCodec, std::string("aac"));
    QVERIFY2(info.videoPackets >= 1, "no video packets");
    QVERIFY2(info.audioPackets >= 1, "no audio packets");
}
```
And add the declaration `void encodesVideoPlusAudio();` to `private slots:`.

- [ ] **Step 3: Add members and methods to the header**

In `Mp4Encoder.h`, add the forward declarations `struct AVAudioFifo; struct SwrContext;`, and add to the class:
```cpp
    void configureAudio(int sampleRate, int channels);
    bool writeAudio(const int16_t* interleaved, int nbSamples);
private:
    bool drainAudio(bool flush);
    // audio
    bool audioConfigured_ = false;
    int  audioSampleRate_ = 0;
    int  audioChannels_ = 0;
    AVCodecContext* aCodec_ = nullptr;
    AVStream*       aStream_ = nullptr;
    AVFrame*        aFrame_ = nullptr;
    AVAudioFifo*    fifo_ = nullptr;
    SwrContext*     swr_ = nullptr;
    int64_t         aPts_ = 0;
    std::mutex      muxMutex_;   // protects writes to fmt_ (video thread + audio thread concurrency)
```
Add `#include <mutex>` at the top.

- [ ] **Step 4: Implementation (configureAudio / create stream in open / writeAudio / drainAudio / finish extension / destructor cleanup)**

Key points (write the full implementation into `Mp4Encoder.cpp`):
- `configureAudio`: just record `audioSampleRate_/audioChannels_/audioConfigured_=true`.
- `open()`: **before** `avformat_write_header`, if `audioConfigured_`:
  - `enc = avcodec_find_encoder(AV_CODEC_ID_AAC)`; `aStream_ = avformat_new_stream`;
  - `aCodec_`: `sample_fmt=AV_SAMPLE_FMT_FLTP`, `sample_rate=audioSampleRate_`, `bit_rate=128000`, channel layout via `av_channel_layout_default(&aCodec_->ch_layout, audioChannels_)`, `time_base={1,sampleRate}`; GLOBAL_HEADER same as video; `avcodec_open2`; `avcodec_parameters_from_context(aStream_->codecpar, aCodec_)`.
  - create `fifo_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, channels, 1)`; `swr_`: in S16 interleaved/sampleRate/layout -> out FLTP/sampleRate/layout (`swr_alloc_set_opts2`) + `swr_init`; allocate `aFrame_` and set `nb_samples=aCodec_->frame_size`, format=FLTP, ch_layout, `av_frame_get_buffer`.
- `writeAudio(int16_t* in, nb)`: take `std::lock_guard lk(muxMutex_)`;
  - swr converts S16 interleaved to an FLTP temporary buffer (use `swr_convert` to output into a temporary `uint8_t* conv[CH]`, or convert directly into the fifo); push into `av_audio_fifo_write`;
  - while `av_audio_fifo_size(fifo_) >= frame_size`: `av_audio_fifo_read` one frame into `aFrame_`, set `aFrame_->pts = aPts_; aPts_ += frame_size`, `avcodec_send_frame(aCodec_, aFrame_)`, `drainAudio(false)`.
- `drainAudio(bool flush)`: loop over `avcodec_receive_packet(aCodec_,...)`, rescale to `aStream_->time_base`, set `stream_index=aStream_->index`, `av_interleaved_write_frame`.
- `writeFrame`/`drainPackets` (video): take `std::lock_guard lk(muxMutex_)` in the `writeFrame` body (mutually exclusive with audio when writing to the muxer).
- `finish()`: take the lock; first flush video (send nullptr + drain), then flush audio (send nullptr + `drainAudio(true)`), then `av_write_trailer`; remain idempotent.
- Destructor: free `swr_` (`swr_free`), `fifo_` (`av_audio_fifo_free`), `aFrame_`, `aCodec_`.

> Refer to the new ffmpeg 5.x/6.x channel layout API (`AVChannelLayout ch_layout` + `swr_alloc_set_opts2`). Use the local libav version as reported by `src/ffmpeg_probe.cpp`; if it is the old API, use `channel_layout`/`swr_alloc_set_opts`.

- [ ] **Step 5: Build and run**

```bash
cmake --build build-dev --target test_mp4_encoder
ctest --test-dir build-dev -R test_mp4_encoder --output-on-failure
```
Expected: PASS (the three cases `encodesPlayableMp4` / `interruptedFileStillPlayable` / `encodesVideoPlusAudio`, with audioStreams==1, audioCodec=="aac", audioPackets>=1).

- [ ] **Step 6: Commit**

```bash
git add src/recording/Mp4Encoder.* tests/recording/mp4_probe.* tests/recording/test_mp4_encoder.cpp
git commit -m "feat(audio): Mp4Encoder optional AAC audio track (TDD, synthetic)"
```

---

### Task 2: AudioSource -- capture the system default input via libavdevice pulse (device-guarded integration test)

**Files:**
- Create: `src/recording/AudioSource.h`, `src/recording/AudioSource.cpp`
- Create: `tests/recording/test_audio_source.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- `class rr::AudioSource`:
  - `bool open();` (pulse input `default`)
  - `bool readSamples(std::vector<int16_t>& interleaved, int& nbSamples);` (decodes to S16 interleaved)
  - `int sampleRate() const; int channels() const;`
  - `void close();`

- [ ] **Step 1: Write the device-guarded integration test**

`tests/recording/test_audio_source.cpp`:
```cpp
#include <QtTest>
#include <cstdlib>
#include <vector>
#include "recording/AudioSource.h"

class TestAudioSource : public QObject {
    Q_OBJECT
private slots:
    void capturesSomeSamples();
};

void TestAudioSource::capturesSomeSamples() {
    if (!std::getenv("PULSE_SERVER") && !std::getenv("XDG_RUNTIME_DIR"))
        QSKIP("no pulse runtime; skipping audio capture test");
    rr::AudioSource src;
    if (!src.open()) QSKIP("cannot open pulse default source in this env");
    QVERIFY(src.sampleRate() > 0);
    QVERIFY(src.channels() > 0);
    std::vector<int16_t> buf; int n = 0;
    QVERIFY(src.readSamples(buf, n));
    QVERIFY(n > 0);
    QVERIFY(int(buf.size()) >= n * src.channels());
    src.close();
}

QTEST_MAIN(TestAudioSource)
#include "test_audio_source.moc"
```

- [ ] **Step 2: Header**

`src/recording/AudioSource.h`:
```cpp
#pragma once
#include <vector>
#include <cstdint>
struct AVFormatContext; struct AVCodecContext; struct AVFrame;
struct AVPacket; struct SwrContext;
namespace rr {
class AudioSource {
public:
    ~AudioSource();
    bool open();
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
```

- [ ] **Step 3: Implementation**

`src/recording/AudioSource.cpp` (key points):
- `avdevice_register_all()`; `ifmt = av_find_input_format("pulse")`; `avformat_open_input(&fmt_, "default", ifmt, nullptr)`.
- Find the audio stream, create the decoder, `avcodec_open2`.
- `sampleRate_ = dec_->sample_rate; channels_ = dec_->ch_layout.nb_channels` (old API: `dec_->channels`).
- `swr_`: in = decoder's actual sample_fmt/layout/rate -> out = S16 interleaved/same layout/same rate; `swr_init`.
- `readSamples`: `av_read_frame` -> `avcodec_send_packet` -> `avcodec_receive_frame`; `swr_convert` into an S16 interleaved buffer; `nbSamples=frame_->nb_samples`; `interleaved.resize(nbSamples*channels_)`.
- `close()` frees everything.

- [ ] **Step 4: CMake**

Append `src/recording/AudioSource.cpp` to the `rr_core` sources. Append at the end:
```cmake
add_executable(test_audio_source tests/recording/test_audio_source.cpp)
target_link_libraries(test_audio_source PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_audio_source COMMAND test_audio_source)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_audio_source
ctest --test-dir build-dev -R test_audio_source --output-on-failure
```
Expected: PASS or SKIP (on a machine with a pulse default source it should PASS: reads >0 samples).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/recording/AudioSource.* tests/recording/test_audio_source.cpp
git commit -m "feat(audio): AudioSource via libavdevice pulse default input"
```

---

### Task 3: Recorder dual-track concurrency + wire up audioEnabled (integration test)

**Files:**
- Modify: `src/recording/Recorder.cpp`
- Modify: `tests/recording/test_recorder.cpp`

**Interfaces:** unchanged (`start`/`stop`/`finished`/`error`). Internally: when `options.audioEnabled` is true, open `AudioSource`, call `encoder.configureAudio(sr,ch)`, start an audio thread looping `readSamples -> encoder.writeAudio`, the video loop unchanged; `stop()` makes both exit and then calls `finish()`.

- [ ] **Step 1: Modify runLoop to support the audio thread**

In `Recorder.cpp` runLoop, before `encoder.open`, if `options.audioEnabled`:
```cpp
    AudioSource audio;
    bool useAudio = options.audioEnabled && audio.open();
    if (useAudio) encoder.configureAudio(audio.sampleRate(), audio.channels());
```
(`encoder.open` must come after `configureAudio` -- adjust the order: first source/audio open, then configureAudio, then encoder.open.)
Audio thread:
```cpp
    std::thread audioThread;
    if (useAudio) {
        audioThread = std::thread([&]{
            std::vector<int16_t> buf; int n = 0;
            while (!stopFlag_.load()) {
                if (!audio.readSamples(buf, n)) break;
                if (n > 0) encoder.writeAudio(buf.data(), n);
            }
        });
    }
```
After the video loop ends:
```cpp
    if (audioThread.joinable()) audioThread.join();
    audio.close();
    if (!encoder.finish()) { emit error(...); return; }
```
Add `#include "recording/AudioSource.h"` and `<thread>` at the top.
> `Mp4Encoder`'s internal `muxMutex_` already guarantees concurrency safety for `writeFrame`/`writeAudio`.

- [ ] **Step 2: Integration test (DISPLAY + pulse guards)**

Add to `tests/recording/test_recorder.cpp`:
```cpp
void TestRecorder::recordsWithAudio() {
    if (!std::getenv("DISPLAY")) QSKIP("no DISPLAY");
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const QString path = dir.path() + "/av.mp4";
    rr::Recorder rec;
    QSignalSpy finishedSpy(&rec, &rr::Recorder::finished);
    QSignalSpy errorSpy(&rec, &rr::Recorder::error);
    rr::CaptureRegion region{0,0,320,240,0,1.0};
    rr::OutputOptions opts; opts.path = path.toStdString(); opts.fps = 10;
    opts.audioEnabled = true;
    rec.start(region, opts);
    QTest::qWait(3000);
    rec.stop();
    QVERIFY(finishedSpy.wait(6000));
    QCOMPARE(errorSpy.count(), 0);
    const rr::test::Mp4Info info = rr::test::probeMp4(path.toStdString());
    QVERIFY(info.ok);
    QCOMPARE(info.codec, std::string("h264"));
    QVERIFY2(info.videoPackets >= 3, "too few video frames");
    // with pulse present there should be an audio track; without it, tolerate (useAudio=false)
    if (info.audioStreams > 0) {
        QCOMPARE(info.audioCodec, std::string("aac"));
        QVERIFY(info.audioPackets >= 1);
    }
}
```
And declare it in `private slots:`. Requires `#include "recording/mp4_probe.h"` (already present).

- [ ] **Step 3: Build and run + full regression**

```bash
cmake --build build-dev
QT_QPA_PLATFORM=offscreen DISPLAY=:1 ctest --test-dir build-dev --output-on-failure
```
Expected: all PASS (including `recordsWithAudio`; on a machine with a pulse default source it should record an MP4 with an aac track).

- [ ] **Step 4: Manual verification (optional)**

```bash
./build-dev/RegionRecord   # check "record audio", record a clip, use ffprobe to see whether it contains an aac stream
```

- [ ] **Step 5: Commit**

```bash
git add src/recording/Recorder.cpp tests/recording/test_recorder.cpp
git commit -m "feat(audio): Recorder dual-track capture, wire audioEnabled"
```

---

## Self-Review

**1. Coverage:** turns `audioEnabled` from a "decoration" into a real feature -- encoder audio track (Task1) + capture source (Task2) + concurrent orchestration (Task3). The spec "support audio recording, off by default, system default input" is achieved.

**2. Placeholder:** Task1 contains the complete test and implementation key points; Task2/3 provide the key code and ffmpeg API notes (old vs. new channel layout). When implementing, choose the API based on the local libav version.

**3. Consistency:** `Mp4Encoder::{configureAudio,writeAudio}`, `AudioSource::{open,readSamples,sampleRate,channels,close}`, and the `Mp4Info` audio fields are consistent across Tasks; `muxMutex_` guarantees concurrent-write safety.

**4. Known limitations:** v1 does no fine A/V drift correction (long recordings may drift slightly out of sync); pulse backend only (without pulse, useAudio=false, silently falls back to video-only with no error). Win/mac audio sources are a later plan.
