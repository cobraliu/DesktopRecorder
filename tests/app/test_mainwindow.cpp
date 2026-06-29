#include <QtTest>
#include "ui/MainWindow.h"

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void constructsAndShows();
};

void TestMainWindow::constructsAndShows() {
    rr::MainWindow w;
    w.show();
    QVERIFY(w.isVisible());
    w.close();
}

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
