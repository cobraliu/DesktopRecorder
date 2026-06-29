#include <QtTest>
#include "ui/frame_geometry.h"

class TestFrameGeometry : public QObject {
    Q_OBJECT
private slots:
    void holeInsetsByBorder();
    void roundTrip();
};

void TestFrameGeometry::holeInsetsByBorder() {
    const rr::CaptureRegion h = rr::holeRegionFromFrame(QRect(100, 100, 408, 308), 4);
    QCOMPARE(h.x, 104);
    QCOMPARE(h.y, 104);
    QCOMPARE(h.w, 400);
    QCOMPARE(h.h, 300);
}

void TestFrameGeometry::roundTrip() {
    rr::CaptureRegion r; r.x = 200; r.y = 150; r.w = 640; r.h = 480;
    const QRect f = rr::frameGeomForRegion(r, 6);
    QCOMPARE(f, QRect(194, 144, 652, 492));
    const rr::CaptureRegion h = rr::holeRegionFromFrame(f, 6);
    QCOMPARE(h.x, r.x);
    QCOMPARE(h.y, r.y);
    QCOMPARE(h.w, r.w);
    QCOMPARE(h.h, r.h);
}

QTEST_MAIN(TestFrameGeometry)
#include "test_frame_geometry.moc"
