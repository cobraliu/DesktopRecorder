#include <QtTest>
#include <QSignalSpy>
#include "ui/CountdownOverlay.h"

class TestOverlays : public QObject {
    Q_OBJECT
private slots:
    void zeroSecondFiresImmediately();
    void countdownReachesZero();
};

void TestOverlays::zeroSecondFiresImmediately() {
    rr::CountdownOverlay ov;
    QSignalSpy spy(&ov, &rr::CountdownOverlay::countdownFinished);
    ov.start(0);
    QCOMPARE(spy.count(), 1);
}

void TestOverlays::countdownReachesZero() {
    rr::CountdownOverlay ov;
    QSignalSpy spy(&ov, &rr::CountdownOverlay::countdownFinished);
    ov.start(1);
    QVERIFY(spy.wait(3000));
}

QTEST_MAIN(TestOverlays)
#include "test_overlays.moc"
