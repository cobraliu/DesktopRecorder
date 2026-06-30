#include "recording/Recorder.h"
#include "recording/FrameSource.h"
#include "recording/AudioSource.h"
#include "recording/Mp4Encoder.h"

#include <QThread>
#include <thread>
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
    if (thread_) return;          // already recording: ignore re-entry to avoid overwriting the thread pointer and leaking
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
    auto sourcePtr = createFrameSource();
    if (!sourcePtr || !sourcePtr->open(region, options.fps)) {
        emit error(QStringLiteral("Failed to open screen capture source"));
        return;
    }
    FrameSource& source = *sourcePtr;

    // Optional audio: open the capture source first, then configure the encoder from its parameters (must be before encoder.open)
    AudioSource audio;
    const bool useAudio = options.audioEnabled && audio.open();

    Mp4Encoder encoder;
    if (useAudio) encoder.configureAudio(audio.sampleRate(), audio.channels());
    if (!encoder.open(source.width(), source.height(), options.fps, options.path)) {
        if (useAudio) audio.close();
        emit error(QStringLiteral("Failed to create encoder/output file"));
        return;
    }

    // Audio thread: feeds the same encoder concurrently with the video loop (its internal mutex keeps writes safe)
    std::thread audioThread;
    if (useAudio) {
        audioThread = std::thread([&] {
            std::vector<int16_t> buf;
            int n = 0;
            while (!stopFlag_.load()) {
                if (!audio.readSamples(buf, n)) break;
                if (n > 0) encoder.writeAudio(buf.data(), n);
            }
        });
    }

    std::vector<uint8_t> rgb;
    int stride = 0;
    bool writeFailed = false;
    while (!stopFlag_.load()) {
        if (!source.readFrame(rgb, stride)) break;
        if (!encoder.writeFrame(rgb.data(), stride)) { writeFailed = true; break; }
    }

    if (audioThread.joinable()) audioThread.join();
    source.close();
    audio.close();

    if (writeFailed) {
        emit error(QStringLiteral("Failed to write frame"));
        return;
    }
    if (!encoder.finish()) {
        emit error(QStringLiteral("Failed to finalize the output file"));
        return;
    }
    emit finished(QString::fromStdString(options.path));
}

}
