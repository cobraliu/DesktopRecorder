#include <QtTest>
#include <QTemporaryDir>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "recording/Mp4Encoder.h"
#include "recording/mp4_probe.h"

class TestMp4Encoder : public QObject {
    Q_OBJECT
private slots:
    void encodesPlayableMp4();
    void interruptedFileStillPlayable();
    void encodesVideoPlusAudio();
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
        // Intentionally do not call finish(); the destructor runs at scope exit without writing a trailer
    }

    const rr::test::Mp4Info info = rr::test::probeMp4(path);
    QVERIFY2(info.ok, "interrupted file not openable");
    QVERIFY2(info.videoPackets >= 1, "interrupted fragmented mp4 had no packets");
}

void TestMp4Encoder::encodesVideoPlusAudio() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = (dir.path() + "/av.mp4").toStdString();

    const int W = 320, H = 240, FPS = 30, N = 30;
    const int SR = 44100, CH = 2;
    rr::Mp4Encoder enc;
    enc.configureAudio(SR, CH);
    QVERIFY(enc.open(W, H, FPS, path));

    std::vector<uint8_t> rgb(static_cast<size_t>(W) * H * 3, 0);
    const int samplesPerFrame = SR / FPS;
    std::vector<int16_t> pcm(static_cast<size_t>(samplesPerFrame) * CH);
    for (int f = 0; f < N; ++f) {
        std::fill(rgb.begin(), rgb.end(), static_cast<uint8_t>(f * 8));
        QVERIFY(enc.writeFrame(rgb.data(), W * 3));
        for (int s = 0; s < samplesPerFrame; ++s) {
            const int16_t v = static_cast<int16_t>((s + f * 131) % 2000 - 1000);
            pcm[static_cast<size_t>(s) * CH] = v;
            pcm[static_cast<size_t>(s) * CH + 1] = v;
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

QTEST_APPLESS_MAIN(TestMp4Encoder)
#include "test_mp4_encoder.moc"
