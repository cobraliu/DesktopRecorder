// Export the application icon (rr::renderAppIcon) to PNGs for packaging.
// Usage: render_icon <out-dir>  -> writes icon_<px>.png for each size.
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <cstdio>
#include "ui/icons.h"

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");
    QDir().mkpath(outDir);

    const int sizes[] = {16, 24, 32, 48, 64, 128, 256, 512, 1024};
    for (int px : sizes) {
        const QImage img = rr::renderAppIcon(px);
        const QString path = QStringLiteral("%1/icon_%2.png").arg(outDir).arg(px);
        if (!img.save(path, "PNG")) {
            std::fprintf(stderr, "failed to write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("wrote %s\n", qPrintable(path));
    }
    return 0;
}
