# RegionRecord P2 Recording Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the recording engine: capture a screen region into **fragmented MP4 (H.264)**, and on stop wind down gracefully to produce a playable file without blocking the UI. The core encoder gets full coverage via **synthetic-frame TDD**; X11 screen capture is integrated separately as a thin adapter layer.

**Architecture:** `Mp4Encoder` (consumes YUV420P frames -> libx264 encode -> fragmented MP4 muxing -> finalization) is the pure-logic-testable core; the `FrameSource` abstraction + `X11FrameSource` (libavdevice x11grab) is the real screen-capture adapter; `Recorder` (QObject) orchestrates "source -> encoder" on a worker thread, providing start/stop plus finished/error signals. This plan is developed and tested entirely with the **dev preset** (system Qt + ffmpeg), independent of P1's static build.

**Tech Stack:** C++17, libav (avformat/avcodec/avdevice/avutil/swscale), Qt6 Core/Test, QThread.

## Global Constraints

> Project-wide constraints, implicitly included in every Task (taken from the tech-stack selection spec).

- **Graceful stop without hanging**: stop = stop reading frames -> flush encoder -> `av_write_trailer`, run on the worker thread, never synchronously waiting on the UI thread.
- **Playable on crash**: recording uses fragmented MP4 (`movflags = frag_keyframe+empty_moov+default_base_moof`), so even if finalization is interrupted the file is mostly playable.
- **Platform scope v1**: this plan only implements the **Linux (X11)** capture source; Windows/macOS sources belong to P8. The encoder/orchestration are cross-platform shared code.
- **C++ standard**: C++17. UI: Qt Widgets (this plan does not touch the UI, only the engine).
- Video H.264 (libx264), MP4 container; audio belongs to P7, this plan is video only.

## File Structure

| File | Responsibility |
|---|---|
| `src/recording/types.h` | `CaptureRegion` / `OutputOptions` value types + region validation declarations |
| `src/recording/types.cpp` | region validation/clamp implementation |
| `src/recording/Mp4Encoder.h` / `.cpp` | YUV420P frames -> H.264 -> fragmented MP4 + finalization |
| `src/recording/FrameSource.h` | abstract frame-source interface |
| `src/recording/X11FrameSource.h` / `.cpp` | libavdevice x11grab capture source (Linux) |
| `src/recording/Recorder.h` / `.cpp` | QObject orchestration + worker thread + graceful stop |
| `tests/recording/mp4_probe.h` / `.cpp` | test helper: probe MP4 (decode and count frames, get codec/dimensions) |
| `tests/recording/test_types.cpp` | region validation unit tests |
| `tests/recording/test_mp4_encoder.cpp` | synthetic-frame encode/finalize/interrupted-playable unit tests |
| `tests/recording/test_x11_source.cpp` | X11 capture integration test (DISPLAY guard) |
| `tests/recording/test_recorder.cpp` | end-to-end recording + graceful-stop integration test |
| `CMakeLists.txt` | append the above source files and test targets |

---

### Task 1: Value types + region validation (pure-logic TDD)

**Files:**
- Create: `tests/recording/test_types.cpp`
- Create: `src/recording/types.h`
- Create: `src/recording/types.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: none.
- Produces:
  - `struct rr::CaptureRegion { int x, y, w, h; int screenIndex = 0; double dpiScale = 1.0; };`
  - `struct rr::OutputOptions { std::string path; int fps = 30; bool audioEnabled = false; };`
  - `bool rr::isValidRegion(const CaptureRegion&);` -- returns true when w>0 and h>0 and both w and h are even (H.264 YUV420 requires even dimensions).
  - `CaptureRegion rr::normalizeRegion(CaptureRegion);` -- flips negative w/h positive (moving the origin accordingly), and rounds odd w/h down to even.

- [ ] **Step 1: Append recording sources and test target to CMakeLists**

In `CMakeLists.txt`, replace the `add_library(rr_core ...)` section with:
```cmake
add_library(rr_core STATIC
    src/ffmpeg_probe.cpp
    src/recording/types.cpp
)
target_include_directories(rr_core PUBLIC src)
target_link_libraries(rr_core PUBLIC rr_ffmpeg)
```
At the end of the file (after `add_test(NAME test_ffmpeg_probe ...)`) append:
```cmake
add_executable(test_types tests/recording/test_types.cpp)
target_link_libraries(test_types PRIVATE rr_core Qt6::Test)
add_test(NAME test_types COMMAND test_types)
```

- [ ] **Step 2: Write failing tests**

`tests/recording/test_types.cpp`:
```cpp
#include <QtTest>
#include "recording/types.h"

using rr::CaptureRegion;

class TestTypes : public QObject {
    Q_OBJECT
private slots:
    void validRejectsZero();
    void validRejectsOdd();
    void validAcceptsEven();
    void normalizeFlipsNegative();
    void normalizeRoundsOddDown();
};

void TestTypes::validRejectsZero() {
    QVERIFY(!rr::isValidRegion({0, 0, 0, 100, 0, 1.0}));
}
void TestTypes::validRejectsOdd() {
    QVERIFY(!rr::isValidRegion({0, 0, 101, 100, 0, 1.0}));
}
void TestTypes::validAcceptsEven() {
    QVERIFY(rr::isValidRegion({10, 10, 200, 100, 0, 1.0}));
}
void TestTypes::normalizeFlipsNegative() {
    const CaptureRegion n = rr::normalizeRegion({100, 100, -40, -20, 0, 1.0});
    QCOMPARE(n.x, 60); QCOMPARE(n.y, 80);
    QCOMPARE(n.w, 40); QCOMPARE(n.h, 20);
}
void TestTypes::normalizeRoundsOddDown() {
    const CaptureRegion n = rr::normalizeRegion({0, 0, 101, 99, 0, 1.0});
    QCOMPARE(n.w, 100); QCOMPARE(n.h, 98);
}

QTEST_APPLESS_MAIN(TestTypes)
#include "test_types.moc"
```

`src/recording/types.h`:
```cpp
#pragma once
#include <string>

namespace rr {

struct CaptureRegion {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int screenIndex = 0;
    double dpiScale = 1.0;
};

struct OutputOptions {
    std::string path;
    int fps = 30;
    bool audioEnabled = false;
};

bool isValidRegion(const CaptureRegion& r);
CaptureRegion normalizeRegion(CaptureRegion r);

}
```

`src/recording/types.cpp` (**placeholder, makes the tests fail**):
```cpp
#include "recording/types.h"
namespace rr {
bool isValidRegion(const CaptureRegion&) { return false; }
CaptureRegion normalizeRegion(CaptureRegion r) { return r; }
}
```

- [ ] **Step 3: Build and run the tests, confirm failure**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_types --output-on-failure
```
Expected: FAIL (multiple cases do not pass).

- [ ] **Step 4: Implement**

`src/recording/types.cpp` (replace the whole file):
```cpp
#include "recording/types.h"

namespace rr {

bool isValidRegion(const CaptureRegion& r) {
    return r.w > 0 && r.h > 0 && (r.w % 2 == 0) && (r.h % 2 == 0);
}

CaptureRegion normalizeRegion(CaptureRegion r) {
    if (r.w < 0) { r.x += r.w; r.w = -r.w; }
    if (r.h < 0) { r.y += r.h; r.h = -r.h; }
    r.w -= (r.w % 2);
    r.h -= (r.h % 2);
    return r;
}

}
```

- [ ] **Step 5: Build and run the tests, confirm pass**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_types --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

Run:
```bash
git add CMakeLists.txt src/recording/types.h src/recording/types.cpp tests/recording/test_types.cpp
git commit -m "feat(p2): CaptureRegion/OutputOptions + region validation (TDD)"
```

---

### Task 2: Mp4Encoder -- encode synthetic frames to MP4 (TDD)

**Files:**
- Create: `tests/recording/mp4_probe.h`
- Create: `tests/recording/mp4_probe.cpp`
- Create: `tests/recording/test_mp4_encoder.cpp`
- Create: `src/recording/Mp4Encoder.h`
- Create: `src/recording/Mp4Encoder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: none (independent of Task 1).
- Produces:
  - class `rr::Mp4Encoder`, methods:
    - `bool open(int width, int height, int fps, const std::string& path);` -- returns false on failure.
    - `bool writeFrame(const uint8_t* rgb, int stride);` -- pass in one frame of **packed RGB24** (stride width*3 or a custom stride), internally converted to YUV420P and encoded.
    - `bool finish();` -- flush + write trailer + close.
  - test helper `struct rr::test::Mp4Info { bool ok; int width; int height; int videoPackets; std::string codec; };` and `Mp4Info rr::test::probeMp4(const std::string& path);`

- [ ] **Step 1: Append probe helper and test target to CMakeLists**

Append to the end of `CMakeLists.txt`:
```cmake
add_library(rr_core_test_util STATIC tests/recording/mp4_probe.cpp)
target_include_directories(rr_core_test_util PUBLIC tests)
target_link_libraries(rr_core_test_util PUBLIC rr_ffmpeg)

add_executable(test_mp4_encoder tests/recording/test_mp4_encoder.cpp)
target_link_libraries(test_mp4_encoder PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_mp4_encoder COMMAND test_mp4_encoder)
```
And append `Mp4Encoder.cpp` to the `rr_core` sources:
```cmake
add_library(rr_core STATIC
    src/ffmpeg_probe.cpp
    src/recording/types.cpp
    src/recording/Mp4Encoder.cpp
)
target_include_directories(rr_core PUBLIC src)
target_link_libraries(rr_core PUBLIC rr_ffmpeg)
```

- [ ] **Step 2: Write the MP4 probe helper**

`tests/recording/mp4_probe.h`:
```cpp
#pragma once
#include <string>

namespace rr::test {
struct Mp4Info {
    bool ok = false;
    int width = 0;
    int height = 0;
    int videoPackets = 0;
    std::string codec;
};
Mp4Info probeMp4(const std::string& path);
}
```

`tests/recording/mp4_probe.cpp`:
```cpp
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
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vIdx = static_cast<int>(i);
            break;
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
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    info.ok = true;
    return info;
}

}
```
> Note: `mp4_probe.h` lives under `tests/recording/`, but is included as `#include "recording/mp4_probe.h"` -- because `rr_core_test_util` sets `tests` as the include root.

- [ ] **Step 3: Write failing tests**

`tests/recording/test_mp4_encoder.cpp`:
```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <vector>
#include <cstdint>
#include "recording/Mp4Encoder.h"
#include "recording/mp4_probe.h"

class TestMp4Encoder : public QObject {
    Q_OBJECT
private slots:
    void encodesPlayableMp4();
};

void TestMp4Encoder::encodesPlayableMp4() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = (dir.path() + "/out.mp4").toStdString();

    const int W = 320, H = 240, FPS = 30, N = 15;
    rr::Mp4Encoder enc;
    QVERIFY(enc.open(W, H, FPS, path));

    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
    for (int f = 0; f < N; ++f) {
        // fill each frame with a different gray level so frame content varies
        std::fill(rgb.begin(), rgb.end(), static_cast<uint8_t>(f * 15));
        QVERIFY(enc.writeFrame(rgb.data(), W * 3));
    }
    QVERIFY(enc.finish());

    const rr::test::Mp4Info info = rr::test::probeMp4(path);
    QVERIFY(info.ok);
    QCOMPARE(info.width, W);
    QCOMPARE(info.height, H);
    QCOMPARE(info.codec, std::string("h264"));
    QVERIFY2(info.videoPackets >= 1, "no video packets decoded");
}

QTEST_APPLESS_MAIN(TestMp4Encoder)
#include "test_mp4_encoder.moc"
```

`src/recording/Mp4Encoder.h`:
```cpp
#pragma once
#include <string>
#include <cstdint>

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace rr {

class Mp4Encoder {
public:
    Mp4Encoder() = default;
    ~Mp4Encoder();
    Mp4Encoder(const Mp4Encoder&) = delete;
    Mp4Encoder& operator=(const Mp4Encoder&) = delete;

    bool open(int width, int height, int fps, const std::string& path);
    bool writeFrame(const uint8_t* rgb, int stride);
    bool finish();

private:
    bool drainPackets();

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext*  codec_ = nullptr;
    AVStream*        stream_ = nullptr;
    AVFrame*         frame_ = nullptr;
    AVPacket*        pkt_ = nullptr;
    SwsContext*      sws_ = nullptr;
    int   width_ = 0;
    int   height_ = 0;
    int64_t pts_ = 0;
    bool  opened_ = false;
    bool  finished_ = false;
};

}
```

`src/recording/Mp4Encoder.cpp` (**placeholder, everything returns false to make the tests fail**):
```cpp
#include "recording/Mp4Encoder.h"
namespace rr {
Mp4Encoder::~Mp4Encoder() {}
bool Mp4Encoder::open(int, int, int, const std::string&) { return false; }
bool Mp4Encoder::writeFrame(const uint8_t*, int) { return false; }
bool Mp4Encoder::finish() { return false; }
bool Mp4Encoder::drainPackets() { return false; }
}
```

- [ ] **Step 4: Build and run the tests, confirm failure**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_mp4_encoder --output-on-failure
```
Expected: FAIL (`enc.open` returns false).

- [ ] **Step 5: Implement Mp4Encoder**

`src/recording/Mp4Encoder.cpp` (replace the whole file):
```cpp
#include "recording/Mp4Encoder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace rr {

Mp4Encoder::~Mp4Encoder() {
    if (opened_ && !finished_) finish();
    if (sws_)   sws_freeContext(sws_);
    if (frame_) av_frame_free(&frame_);
    if (pkt_)   av_packet_free(&pkt_);
    if (codec_) avcodec_free_context(&codec_);
    if (fmt_) {
        if (fmt_->pb) avio_closep(&fmt_->pb);
        avformat_free_context(fmt_);
        fmt_ = nullptr;
    }
}

bool Mp4Encoder::open(int width, int height, int fps, const std::string& path) {
    width_ = width; height_ = height;

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
    codec_->gop_size = fps; // one keyframe per second, helps fragmented playability
    if (fmt_->oformat->flags & AVFMT_GLOBALHEADER)
        codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(codec_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codec_, enc, nullptr) < 0) return false;
    if (avcodec_parameters_from_context(stream_->codecpar, codec_) < 0) return false;
    stream_->time_base = codec_->time_base;

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

bool Mp4Encoder::writeFrame(const uint8_t* rgb, int stride) {
    if (!opened_) return false;
    if (av_frame_make_writable(frame_) < 0) return false;

    const uint8_t* src[1] = { rgb };
    const int srcStride[1] = { stride };
    sws_scale(sws_, src, srcStride, 0, height_,
              frame_->data, frame_->linesize);

    frame_->pts = pts_++;
    if (avcodec_send_frame(codec_, frame_) < 0) return false;
    return drainPackets();
}

bool Mp4Encoder::finish() {
    if (!opened_ || finished_) return false;
    finished_ = true;
    if (avcodec_send_frame(codec_, nullptr) < 0) return false; // flush
    if (!drainPackets()) return false;
    if (av_write_trailer(fmt_) < 0) return false;
    if (fmt_->pb) avio_closep(&fmt_->pb);
    return true;
}

}
```

- [ ] **Step 6: Build and run the tests, confirm pass**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_mp4_encoder --output-on-failure
```
Expected: PASS (width/height/codec=h264/packets>=1).

- [ ] **Step 7: Commit**

Run:
```bash
git add CMakeLists.txt src/recording/Mp4Encoder.h src/recording/Mp4Encoder.cpp tests/recording/mp4_probe.h tests/recording/mp4_probe.cpp tests/recording/test_mp4_encoder.cpp
git commit -m "feat(p2): Mp4Encoder RGB->H.264 fragmented MP4 (TDD)"
```

---

### Task 3: Playable even when interrupted (fragmented verification, TDD)

**Files:**
- Modify: `tests/recording/test_mp4_encoder.cpp`

**Interfaces:**
- Consumes: `rr::Mp4Encoder`, `rr::test::probeMp4`.
- Produces: new case `interruptedFileStillPlayable` -- write several frames then **do not call finish()** (simulating interrupted finalization); the file can still be probed for at least 1 video packet.

- [ ] **Step 1: Add a failing/verification test case**

Add `void interruptedFileStillPlayable();` to the `private slots:` list of `TestMp4Encoder`, and implement it in the file:
```cpp
void TestMp4Encoder::interruptedFileStillPlayable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = (dir.path() + "/partial.mp4").toStdString();

    const int W = 320, H = 240, FPS = 30, N = 40;
    {
        rr::Mp4Encoder enc;
        QVERIFY(enc.open(W, H, FPS, path));
        std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3);
        for (int f = 0; f < N; ++f) {
            std::fill(rgb.begin(), rgb.end(), static_cast<uint8_t>(f * 6));
            QVERIFY(enc.writeFrame(rgb.data(), W * 3));
        }
        // deliberately do not call finish() -- the destructor stands in for the exception path;
        // to simulate "interrupted finalization", skip finalization here in a leak-like manner:
        enc.~Mp4Encoder(); // explicit destruction then suppress re-destruction
        new (&enc) rr::Mp4Encoder();
    }

    const rr::test::Mp4Info info = rr::test::probeMp4(path);
    QVERIFY2(info.ok, "interrupted file not openable");
    QVERIFY2(info.videoPackets >= 1, "interrupted fragmented mp4 had no packets");
}
```
> Explanation: this case verifies the core value of fragmented MP4 -- even without a written trailer, the fragments already flushed to disk remain parseable. The current `~Mp4Encoder()` implementation calls finish() when not finished; to truly simulate "interrupted", adjust the implementation slightly as below.

- [ ] **Step 2: Make the destructor not auto-finalize when "not finished" (truly simulate interruption)**

Modify the first lines of the `src/recording/Mp4Encoder.cpp` destructor -- remove the "auto-finish if not finished" behavior, and only release resources safely (without writing the trailer), making the interruption semantics real:
```cpp
Mp4Encoder::~Mp4Encoder() {
    // do not auto-write the trailer: interrupted means interrupted, relying on fragmented playability
    if (sws_)   sws_freeContext(sws_);
    if (frame_) av_frame_free(&frame_);
    if (pkt_)   av_packet_free(&pkt_);
    if (codec_) avcodec_free_context(&codec_);
    if (fmt_) {
        if (fmt_->pb) avio_closep(&fmt_->pb);
        avformat_free_context(fmt_);
        fmt_ = nullptr;
    }
}
```
> At the same time, delete the two lines `enc.~Mp4Encoder(); new (&enc) ...` in the Task 3 Step 1 test, and just let the scope end and destruct naturally:
```cpp
        // deliberately do not call finish(); the scope ends and destructs naturally, no trailer written
    }
```

- [ ] **Step 3: Build and run all encoder tests, confirm pass**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_mp4_encoder --output-on-failure
```
Expected: PASS (both `encodesPlayableMp4` and `interruptedFileStillPlayable` pass).

- [ ] **Step 4: Commit**

Run:
```bash
git add src/recording/Mp4Encoder.cpp tests/recording/test_mp4_encoder.cpp
git commit -m "feat(p2): verify fragmented MP4 is playable when finalize interrupted"
```

---

### Task 4: X11FrameSource -- libavdevice x11grab capture (integration test, DISPLAY guard)

**Files:**
- Create: `src/recording/FrameSource.h`
- Create: `src/recording/X11FrameSource.h`
- Create: `src/recording/X11FrameSource.cpp`
- Create: `tests/recording/test_x11_source.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rr::CaptureRegion` (Task 1).
- Produces:
  - abstract `class rr::FrameSource { public: virtual ~FrameSource()=default; virtual bool open(const CaptureRegion&, int fps)=0; virtual bool readFrame(std::vector<uint8_t>& rgb, int& stride)=0; virtual void close()=0; virtual int width() const=0; virtual int height() const=0; };`
  - `class rr::X11FrameSource : public FrameSource` (uses `-f x11grab`, input `:<display>+<x>,<y>`, `video_size=WxH`, converts output frames to RGB24 into `rgb`).

- [ ] **Step 1: Append sources and test to CMakeLists**

Append `src/recording/X11FrameSource.cpp` to the `rr_core` sources. Append to the end of the file:
```cmake
add_executable(test_x11_source tests/recording/test_x11_source.cpp)
target_link_libraries(test_x11_source PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_x11_source COMMAND test_x11_source)
```

- [ ] **Step 2: Write the abstract interface**

`src/recording/FrameSource.h`:
```cpp
#pragma once
#include <vector>
#include <cstdint>
#include "recording/types.h"

namespace rr {
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual bool open(const CaptureRegion& region, int fps) = 0;
    virtual bool readFrame(std::vector<uint8_t>& rgb, int& stride) = 0;
    virtual void close() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
}
```

- [ ] **Step 3: Write the integration test (skip if no DISPLAY)**

`tests/recording/test_x11_source.cpp`:
```cpp
#include <QtTest>
#include <cstdlib>
#include <vector>
#include "recording/X11FrameSource.h"

class TestX11Source : public QObject {
    Q_OBJECT
private slots:
    void capturesOneFrame();
};

void TestX11Source::capturesOneFrame() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping X11 capture test");

    rr::X11FrameSource src;
    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    QVERIFY(src.open(region, 10));
    QCOMPARE(src.width(), 320);
    QCOMPARE(src.height(), 240);

    std::vector<uint8_t> rgb;
    int stride = 0;
    QVERIFY(src.readFrame(rgb, stride));
    QVERIFY(stride >= 320 * 3);
    QVERIFY(rgb.size() >= static_cast<size_t>(stride) * 240);
    src.close();
}

QTEST_APPLESS_MAIN(TestX11Source)
#include "test_x11_source.moc"
```

`src/recording/X11FrameSource.h`:
```cpp
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
```

`src/recording/X11FrameSource.cpp`:
```cpp
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
```

- [ ] **Step 4: Build and run (this machine has DISPLAY, so it should really capture and pass)**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_x11_source --output-on-failure
```
Expected: PASS (this machine has `DISPLAY=:1`, capturing one 320x240 frame). If the environment has no DISPLAY, it SKIPs.

- [ ] **Step 5: Commit**

Run:
```bash
git add CMakeLists.txt src/recording/FrameSource.h src/recording/X11FrameSource.h src/recording/X11FrameSource.cpp tests/recording/test_x11_source.cpp
git commit -m "feat(p2): X11FrameSource via libavdevice x11grab"
```

---

### Task 5: Recorder -- worker-thread orchestration + graceful stop (integration test)

**Files:**
- Create: `src/recording/Recorder.h`
- Create: `src/recording/Recorder.cpp`
- Create: `tests/recording/test_recorder.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rr::FrameSource`/`rr::X11FrameSource`, `rr::Mp4Encoder`, `rr::CaptureRegion`, `rr::OutputOptions`.
- Produces:
  - `class rr::Recorder : public QObject`, methods `void start(const CaptureRegion&, const OutputOptions&);` `void stop();`; signals `void finished(const QString& path);` `void error(const QString& msg);`. Internally uses a `QThread` running a "readFrame -> writeFrame" loop; `stop()` sets an atomic flag so the loop exits, then `finish()` finalizes, then emits `finished`.

- [ ] **Step 1: Append to CMakeLists (Recorder needs AUTOMOC, already enabled on rr_core)**

Append `src/recording/Recorder.cpp` to the `rr_core` sources. `rr_core` needs to link Qt6::Core to use QObject/QThread:
```cmake
add_library(rr_core STATIC
    src/ffmpeg_probe.cpp
    src/recording/types.cpp
    src/recording/Mp4Encoder.cpp
    src/recording/X11FrameSource.cpp
    src/recording/Recorder.cpp
)
target_include_directories(rr_core PUBLIC src)
target_link_libraries(rr_core PUBLIC rr_ffmpeg Qt6::Core)
```
Append to the end of the file:
```cmake
add_executable(test_recorder tests/recording/test_recorder.cpp)
target_link_libraries(test_recorder PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_recorder COMMAND test_recorder)
```

- [ ] **Step 2: Write the integration test (DISPLAY guard)**

`tests/recording/test_recorder.cpp`:
```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cstdlib>
#include "recording/Recorder.h"
#include "recording/mp4_probe.h"

class TestRecorder : public QObject {
    Q_OBJECT
private slots:
    void recordsAndFinalizes();
};

void TestRecorder::recordsAndFinalizes() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping recorder integration test");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/rec.mp4";

    rr::Recorder rec;
    QSignalSpy finishedSpy(&rec, &rr::Recorder::finished);
    QSignalSpy errorSpy(&rec, &rr::Recorder::error);

    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    rr::OutputOptions opts; opts.path = path.toStdString(); opts.fps = 10;

    rec.start(region, opts);
    QTest::qWait(1200);            // record for about 1.2 seconds
    rec.stop();

    QVERIFY(finishedSpy.wait(5000)); // wait for finalization to complete
    QCOMPARE(errorSpy.count(), 0);

    const rr::test::Mp4Info info = rr::test::probeMp4(path.toStdString());
    QVERIFY(info.ok);
    QCOMPARE(info.codec, std::string("h264"));
    QVERIFY2(info.videoPackets >= 3, "too few frames recorded");
}

QTEST_MAIN(TestRecorder)
#include "test_recorder.moc"
```

- [ ] **Step 3: Write the Recorder header**

`src/recording/Recorder.h`:
```cpp
#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include "recording/types.h"

class QThread;

namespace rr {

class Recorder : public QObject {
    Q_OBJECT
public:
    explicit Recorder(QObject* parent = nullptr);
    ~Recorder() override;

    void start(const CaptureRegion& region, const OutputOptions& options);
    void stop();

signals:
    void finished(const QString& path);
    void error(const QString& msg);

private:
    void runLoop(CaptureRegion region, OutputOptions options);

    QThread* thread_ = nullptr;
    std::atomic<bool> stopFlag_{false};
};

}
```

- [ ] **Step 4: Write the Recorder implementation**

`src/recording/Recorder.cpp`:
```cpp
#include "recording/Recorder.h"
#include "recording/X11FrameSource.h"
#include "recording/Mp4Encoder.h"

#include <QThread>
#include <vector>

namespace rr {

Recorder::Recorder(QObject* parent) : QObject(parent) {}

Recorder::~Recorder() {
    stop();
    if (thread_) {
        thread_->quit();
        thread_->wait();
        delete thread_;
        thread_ = nullptr;
    }
}

void Recorder::start(const CaptureRegion& region, const OutputOptions& options) {
    stopFlag_.store(false);
    thread_ = QThread::create([this, region, options] {
        runLoop(region, options);
    });
    thread_->start();
}

void Recorder::stop() {
    stopFlag_.store(true);
}

void Recorder::runLoop(CaptureRegion region, OutputOptions options) {
    X11FrameSource source;
    if (!source.open(region, options.fps)) {
        emit error(QStringLiteral("Failed to open screen capture source"));
        return;
    }
    Mp4Encoder encoder;
    if (!encoder.open(source.width(), source.height(), options.fps, options.path)) {
        emit error(QStringLiteral("Failed to create encoder/output file"));
        return;
    }

    std::vector<uint8_t> rgb;
    int stride = 0;
    while (!stopFlag_.load()) {
        if (!source.readFrame(rgb, stride)) break;
        if (!encoder.writeFrame(rgb.data(), stride)) {
            emit error(QStringLiteral("Failed to write frame"));
            return;
        }
    }
    source.close();
    if (!encoder.finish()) {
        emit error(QStringLiteral("Failed to finalize and write file"));
        return;
    }
    emit finished(QString::fromStdString(options.path));
}

}
```

- [ ] **Step 5: Build and run the test, confirm pass**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_recorder --output-on-failure
```
Expected: PASS (records >=3 frames, finished fires, no error).

- [ ] **Step 6: Run all tests to ensure no regression**

Run:
```bash
ctest --test-dir build-dev --output-on-failure
```
Expected: all PASS (types / mp4_encoder / x11_source / recorder / ffmpeg_probe).

- [ ] **Step 7: Commit**

Run:
```bash
git add CMakeLists.txt src/recording/Recorder.h src/recording/Recorder.cpp tests/recording/test_recorder.cpp
git commit -m "feat(p2): Recorder worker-thread orchestration + graceful stop"
```

---

## Self-Review

**1. Spec coverage (P2 scope):**
- Region -> MP4 recording -> Task 2 (encoder) + Task 4 (X11 source) + Task 5 (orchestration).
- Graceful stop without blocking -> Task 5 (worker thread + atomic stop flag + emit signal after finish).
- fragmented MP4 / playable when finalization interrupted -> Task 2/3.
- Value types CaptureRegion/OutputOptions -> Task 1.
- Audio / Windows / macOS sources are out of scope for this plan (P7/P8), as noted in Global Constraints.

**2. Placeholder scan:** no TBDs; every code step contains complete code; placeholder implementations are only used for the TDD "fail first" step and are replaced with real implementations within the same task.

**3. Type consistency:** `CaptureRegion`/`OutputOptions` fields are defined in Task 1 and used consistently in Task 4/5; the signatures `Mp4Encoder::{open,writeFrame,finish}`, `FrameSource::{open,readFrame,close,width,height}`, `Recorder::{start,stop,finished,error}` are consistent across tasks; the test helpers `rr::test::probeMp4`/`Mp4Info` are defined in Task 2 and reused in Task 3/5.
