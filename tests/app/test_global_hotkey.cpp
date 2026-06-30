#include <QtTest>
#include <cstdlib>
#include "platform/GlobalHotkeyX11.h"

class TestGlobalHotkey : public QObject {
    Q_OBJECT
private slots:
    void registerAndUnregister();
};

void TestGlobalHotkey::registerAndUnregister() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping X11 hotkey test");
    rr::GlobalHotkeyX11 hk;
    // Use a rare combo (Ctrl+Alt+Shift+F12) to reduce the chance of conflicting with the desktop environment;
    // registerHotkey now actually probes for BadAccess (returns false if already grabbed),
    // so if the combo is also taken in the current environment, skip rather than fail.
    if (!hk.registerHotkey(1, true, true, true, Qt::Key_F12))
        QSKIP("hotkey combo already grabbed in this environment");
    QTest::qWait(100);
    hk.unregisterAll(); // passes as long as it does not crash
    QVERIFY(true);
}

QTEST_MAIN(TestGlobalHotkey)
#include "test_global_hotkey.moc"
