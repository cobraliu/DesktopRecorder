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
    void recordsWithAudio();
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
    // Opening x11grab + find_stream_info consumes about 1 second of wall-clock time up front,
    // so leave a long enough recording window to ensure the capture loop is actually entered
    QTest::qWait(3000);
    rec.stop();

    QVERIFY(finishedSpy.wait(5000)); // wait for finalization to complete
    QCOMPARE(errorSpy.count(), 0);

    const rr::test::Mp4Info info = rr::test::probeMp4(path.toStdString());
    QVERIFY(info.ok);
    QCOMPARE(info.codec, std::string("h264"));
    QVERIFY2(info.videoPackets >= 3, "too few frames recorded");
}

void TestRecorder::recordsWithAudio() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping recorder audio integration test");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/av.mp4";

    rr::Recorder rec;
    QSignalSpy finishedSpy(&rec, &rr::Recorder::finished);
    QSignalSpy errorSpy(&rec, &rr::Recorder::error);

    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    rr::OutputOptions opts;
    opts.path = path.toStdString();
    opts.fps = 10;
    opts.audioEnabled = true;

    rec.start(region, opts);
    QTest::qWait(3000);
    rec.stop();

    QVERIFY(finishedSpy.wait(6000));
    QCOMPARE(errorSpy.count(), 0);

    const rr::test::Mp4Info info = rr::test::probeMp4(path.toStdString());
    QVERIFY(info.ok);
    QCOMPARE(info.codec, std::string("h264"));
    QVERIFY2(info.videoPackets >= 3, "too few video frames recorded");
    // When pulse is available, an aac track should be recorded; if the environment has no pulse, it falls back to video-only, which is tolerated.
    if (info.audioStreams > 0) {
        QCOMPARE(info.audioCodec, std::string("aac"));
        QVERIFY2(info.audioPackets >= 1, "audio stream present but no packets");
    }
}

QTEST_MAIN(TestRecorder)
#include "test_recorder.moc"
