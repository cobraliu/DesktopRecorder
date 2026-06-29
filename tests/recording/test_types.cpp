#include <QtTest>
#include "recording/types.h"

using rr::CaptureRegion;

class TestTypes : public QObject {
    Q_OBJECT
private slots:
    void validRejectsZero();
    void validRejectsOdd();
    void validAcceptsEven();
    void normalizeFlipsNegative();
    void normalizeRoundsOddDown();
};

void TestTypes::validRejectsZero() {
    QVERIFY(!rr::isValidRegion({0, 0, 0, 100, 0, 1.0}));
}
void TestTypes::validRejectsOdd() {
    QVERIFY(!rr::isValidRegion({0, 0, 101, 100, 0, 1.0}));
}
void TestTypes::validAcceptsEven() {
    QVERIFY(rr::isValidRegion({10, 10, 200, 100, 0, 1.0}));
}
void TestTypes::normalizeFlipsNegative() {
    const CaptureRegion n = rr::normalizeRegion({100, 100, -40, -20, 0, 1.0});
    QCOMPARE(n.x, 60); QCOMPARE(n.y, 80);
    QCOMPARE(n.w, 40); QCOMPARE(n.h, 20);
}
void TestTypes::normalizeRoundsOddDown() {
    const CaptureRegion n = rr::normalizeRegion({0, 0, 101, 99, 0, 1.0});
    QCOMPARE(n.w, 100); QCOMPARE(n.h, 98);
}

QTEST_APPLESS_MAIN(TestTypes)
#include "test_types.moc"
