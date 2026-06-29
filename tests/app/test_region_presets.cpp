#include <QtTest>
#include "app/RegionPresets.h"
#include "recording/types.h"

class TestRegionPresets : public QObject {
    Q_OBJECT
private slots:
    void presetsAreEvenAndValid();
    void presetCenteredWithinScreen();
    void presetClampedToScreen();
    void fullscreenMatchesScreenEven();
};

void TestRegionPresets::presetsAreEvenAndValid() {
    const auto ps = rr::standardPresets();
    QVERIFY(ps.size() >= 3);
    for (const auto& p : ps) {
        QVERIFY(p.w % 2 == 0);
        QVERIFY(p.h % 2 == 0);
        QVERIFY(p.w > 0 && p.h > 0);
    }
}

void TestRegionPresets::presetCenteredWithinScreen() {
    rr::Preset p{"720p", 1280, 720};
    auto r = rr::regionFromPreset(p, 0, 0, 1920, 1080);
    QCOMPARE(r.w, 1280);
    QCOMPARE(r.h, 720);
    QCOMPARE(r.x, (1920 - 1280) / 2);
    QCOMPARE(r.y, (1080 - 720) / 2);
    QVERIFY(rr::isValidRegion(r));
}

void TestRegionPresets::presetClampedToScreen() {
    rr::Preset p{"1080p", 1920, 1080};
    auto r = rr::regionFromPreset(p, 0, 0, 1366, 768);
    QVERIFY(r.x >= 0 && r.y >= 0);
    QVERIFY(r.w <= 1366 && r.h <= 768);
    QVERIFY(r.w % 2 == 0 && r.h % 2 == 0);
    QVERIFY(rr::isValidRegion(r));
}

void TestRegionPresets::fullscreenMatchesScreenEven() {
    auto r = rr::fullscreenRegion(100, 50, 1921, 1081);
    QCOMPARE(r.x, 100);
    QCOMPARE(r.y, 50);
    QCOMPARE(r.w, 1920); // 1921 rounded down to even
    QCOMPARE(r.h, 1080);
    QVERIFY(rr::isValidRegion(r));
}

QTEST_APPLESS_MAIN(TestRegionPresets)
#include "test_region_presets.moc"
