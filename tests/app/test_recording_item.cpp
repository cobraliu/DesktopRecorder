#include <QtTest>
#include "app/RecordingItem.h"

class TestRecordingItem : public QObject {
    Q_OBJECT
private slots:
    void labelsAreNonEmpty();
    void jsonRoundTrip();
};

void TestRecordingItem::labelsAreNonEmpty() {
    using S = rr::RecordingState;
    for (S s : {S::Recording, S::Finalizing, S::Completed,
                S::FinalizationInterrupted, S::Failed})
        QVERIFY(!rr::stateLabel(s).isEmpty());
}

void TestRecordingItem::jsonRoundTrip() {
    rr::RecordingItem a;
    a.id = "abc"; a.filePath = "/tmp/x.mp4";
    a.state = rr::RecordingState::Completed;
    a.createdAtMs = 1700000000000LL; a.durationMs = 4200;
    const rr::RecordingItem b = rr::fromJson(rr::toJson(a));
    QCOMPARE(b.id, a.id);
    QCOMPARE(b.filePath, a.filePath);
    QCOMPARE(int(b.state), int(a.state));
    QCOMPARE(b.createdAtMs, a.createdAtMs);
    QCOMPARE(b.durationMs, a.durationMs);
}

QTEST_APPLESS_MAIN(TestRecordingItem)
#include "test_recording_item.moc"
