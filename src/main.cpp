#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QStyleFactory>
#include "ui/MainWindow.h"
#include "ui/theme.h"
#include "ui/icons.h"

#if defined(QT_STATIC)
#include <QtPlugin>
#if defined(Q_OS_WIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined(Q_OS_MAC)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined(Q_OS_LINUX)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
// Native Wayland sessions select this plugin instead of xcb (XWayland). Both are
// imported; Qt picks one at startup from XDG_SESSION_TYPE / WAYLAND_DISPLAY.
Q_IMPORT_PLUGIN(QWaylandIntegrationPlugin)
#endif
#endif

static void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    const QColor bg(0x15, 0x18, 0x1d), base(0x27, 0x2d, 0x35), text(0xee, 0xf1, 0xf4);
    const QColor accent(0x19, 0xc3, 0xa3);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, base);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(0x08, 0x11, 0x0f));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7d, 0x88, 0x93));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x7d, 0x88, 0x93));
    app.setPalette(p);

    app.setStyleSheet(rr::darkTealStyleSheet());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RegionRecord"));
    QApplication::setOrganizationName(QStringLiteral("insnap"));
    QApplication::setWindowIcon(rr::appIcon());
    applyDarkTheme(app);
    rr::MainWindow w;
    w.show();
    return app.exec();
}
