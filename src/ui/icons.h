#pragma once
#include <QIcon>
#include <QPixmap>
#include <QColor>

// Self-drawn vector icons (QPainter) replacing emoji: consistent across platforms, themable with
// the accent color, and crisp on HiDPI. Monochrome stroke style with uniform 1.6px strokes and
// rounded caps, conforming to icon-style-consistent.
namespace rr {

QIcon iconFullscreen(const QColor& c, int px = 18);  // monitor
QIcon iconFrame(const QColor& c, int px = 18);       // region select (corner brackets)
QIcon iconWindow(const QColor& c, int px = 18);      // window (with title bar)
QIcon iconPlay(const QColor& c, int px = 16);        // play triangle
QIcon iconTrash(const QColor& c, int px = 16);       // delete
QIcon iconRecord(const QColor& c, int px = 16);      // solid dot (record)
QIcon iconStop(const QColor& c, int px = 16);        // solid rounded square (stop)
QIcon iconFolder(const QColor& c, int px = 16);      // folder (open directory)

QPixmap statusDot(const QColor& c, int px = 12);     // solid status dot

}
