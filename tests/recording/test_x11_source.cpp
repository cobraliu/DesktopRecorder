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
