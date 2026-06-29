#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cstdlib>
#include "app/RecordingController.h"
#include "app/RecordingStore.h"

class TestRecordingController : public QObject {
    Q_OBJECT
private slots:
    void fullCycleUpdatesStore();
    void badOutputPathSetsFailed();
};

void TestRecordingController::fullCycleUpdatesStore() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping controller integration test");

    QTemporaryDir dir;
    rr::RecordingStore store(dir.path() + "/r.json");
    rr::RecordingController ctl(&store);
    QSignalSpy doneSpy(&ctl, &rr::RecordingController::recordingCompleted);

    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    rr::OutputOptions opts;
    opts.path = (dir.path() + "/c.mp4").toStdString();
    opts.fps = 10;

    const QString id = ctl.startRecording(region, opts);
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.items().size(), 1);
    QCOMPARE(int(store.items()[0].state), int(rr::RecordingState::Recording));

    QTest::qWait(3000);
    ctl.stopRecording();

    QVERIFY(doneSpy.wait(6000));
    QCOMPARE(int(store.items()[0].state), int(rr::RecordingState::Completed));
    QVERIFY(store.items()[0].durationMs > 0);
}

void TestRecordingController::badOutputPathSetsFailed() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping controller failure test");

    QTemporaryDir dir;
    rr::RecordingStore store(dir.path() + "/r.json");
    rr::RecordingController ctl(&store);
    QSignalSpy failSpy(&ctl, &rr::RecordingController::recordingFailed);

    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    rr::OutputOptions opts;
    // Nonexistent directory: after the capture source opens successfully, the encoder fails to create the output file, triggering the error path
    opts.path = "/nonexistent_rr_dir_xyz/out.mp4";
    opts.fps = 10;

    const QString id = ctl.startRecording(region, opts);
    QVERIFY(!id.isEmpty());
    QVERIFY(failSpy.wait(6000));
    QCOMPARE(int(store.items()[0].state), int(rr::RecordingState::Failed));
    QVERIFY(!ctl.isRecording());
}

QTEST_MAIN(TestRecordingController)
#include "test_recording_controller.moc"
