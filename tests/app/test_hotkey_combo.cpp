#include <QtTest>
#include <Qt>
#include "app/HotkeyCombo.h"

class TestHotkeyCombo : public QObject {
    Q_OBJECT
private slots:
    void defaultStopIsAcceptable();
    void rejectsNoModifier();
    void rejectsMetaCombos();
    void rejectsCommonShortcuts();
};

void TestHotkeyCombo::defaultStopIsAcceptable() {
    rr::HotkeyCombo c; c.ctrl = true; c.alt = true; c.key = Qt::Key_S;
    QString why;
    QVERIFY2(rr::isAcceptableHotkey(c, &why), qPrintable(why));
}

void TestHotkeyCombo::rejectsNoModifier() {
    rr::HotkeyCombo c; c.key = Qt::Key_S;
    QVERIFY(!rr::isAcceptableHotkey(c, nullptr));
}

void TestHotkeyCombo::rejectsMetaCombos() {
    rr::HotkeyCombo c; c.meta = true; c.key = Qt::Key_S;
    QVERIFY(!rr::isAcceptableHotkey(c, nullptr));
}

void TestHotkeyCombo::rejectsCommonShortcuts() {
    rr::HotkeyCombo altF4; altF4.alt = true; altF4.key = Qt::Key_F4;
    QVERIFY(!rr::isAcceptableHotkey(altF4, nullptr));
    rr::HotkeyCombo ctrlC; ctrlC.ctrl = true; ctrlC.key = Qt::Key_C;
    QVERIFY(!rr::isAcceptableHotkey(ctrlC, nullptr));
}

QTEST_APPLESS_MAIN(TestHotkeyCombo)
#include "test_hotkey_combo.moc"
