#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "app/RecordingStore.h"

class TestRecordingStore : public QObject {
    Q_OBJECT
private slots:
    void addPersistsAcrossReload();
    void interruptedDetectedOnLoad();
    void removeAndSetState();
};

static rr::RecordingItem mk(const QString& id, rr::RecordingState s) {
    rr::RecordingItem it; it.id = id; it.filePath = "/tmp/" + id + ".mp4";
    it.state = s; it.createdAtMs = 1; it.durationMs = 0; return it;
}

void TestRecordingStore::addPersistsAcrossReload() {
    QTemporaryDir dir;
    const QString path = dir.path() + "/recordings.json";
    {
        rr::RecordingStore s(path);
        QSignalSpy spy(&s, &rr::RecordingStore::changed);
        s.add(mk("a", rr::RecordingState::Completed));
        QVERIFY(spy.count() >= 1);
    }
    rr::RecordingStore s2(path);
    s2.load();
    QCOMPARE(s2.items().size(), 1);
    QCOMPARE(s2.items()[0].id, QString("a"));
}

void TestRecordingStore::interruptedDetectedOnLoad() {
    QTemporaryDir dir;
    const QString path = dir.path() + "/recordings.json";
    {
        rr::RecordingStore s(path);
        s.add(mk("rec", rr::RecordingState::Recording));   // simulate still recording at the time of the last crash
        s.add(mk("fin", rr::RecordingState::Finalizing));  // simulate finalizing at the time of the last crash
    }
    rr::RecordingStore s2(path);
    s2.load();
    QCOMPARE(s2.items().size(), 2);
    for (const auto& it : s2.items())
        QCOMPARE(int(it.state), int(rr::RecordingState::FinalizationInterrupted));
}

void TestRecordingStore::removeAndSetState() {
    QTemporaryDir dir;
    rr::RecordingStore s(dir.path() + "/r.json");
    s.add(mk("a", rr::RecordingState::Recording));
    s.setState("a", rr::RecordingState::Completed);
    QCOMPARE(int(s.items()[0].state), int(rr::RecordingState::Completed));
    QVERIFY(s.removeAt(0));
    QCOMPARE(s.items().size(), 0);
    QVERIFY(!s.removeAt(0));
}

QTEST_APPLESS_MAIN(TestRecordingStore)
#include "test_recording_store.moc"
