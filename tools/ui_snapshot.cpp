// Render MainWindow offscreen to a PNG for design review. Usage: ui_snapshot <out.png>
#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QStyleFactory>
#include <QTimer>
#include "ui/MainWindow.h"
#include "ui/CaptureFrameWindow.h"
#include "ui/theme.h"
#include "recording/types.h"

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("RegionRecord");
    QApplication::setOrganizationName("insnap");
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x15,0x18,0x1d));
    p.setColor(QPalette::WindowText, QColor(0xee,0xf1,0xf4));
    p.setColor(QPalette::Base, QColor(0x27,0x2d,0x35));
    p.setColor(QPalette::Text, QColor(0xee,0xf1,0xf4));
    p.setColor(QPalette::Button, QColor(0x27,0x2d,0x35));
    p.setColor(QPalette::ButtonText, QColor(0xee,0xf1,0xf4));
    p.setColor(QPalette::Highlight, QColor(0x19,0xc3,0xa3));
    app.setPalette(p);
    app.setStyleSheet(rr::darkTealStyleSheet());

    const QString out = argc > 1 ? argv[1] : "/tmp/ui_snapshot.png";
    const bool frameMode = argc > 2 && QString(argv[2]) == "frame";

    if (frameMode) {
        auto* cf = new rr::CaptureFrameWindow();
        rr::CaptureRegion r; r.x = 0; r.y = 0; r.w = 320; r.h = 200;
        cf->beginEditing(r);
        cf->resize(320 + 8, 200 + 8);
        if (argc > 3 && QString(argv[3]) == "record") cf->enterRecordingStyle();
        app.processEvents();
        QTimer::singleShot(300, [&]{
            app.processEvents();
            cf->grab().save(out);
            app.quit();
        });
        return app.exec();
    }

    rr::MainWindow w;
    w.resize(560, 620);
    w.show();
    app.processEvents();
    QTimer::singleShot(300, [&]{
        app.processEvents();
        w.grab().save(out);
        app.quit();
    });
    return app.exec();
}
