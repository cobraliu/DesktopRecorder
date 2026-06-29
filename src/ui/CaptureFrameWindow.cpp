#include "ui/CaptureFrameWindow.h"
#include "ui/frame_geometry.h"

#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QRegion>
#include <QFontMetrics>
#include <QGuiApplication>
#include <algorithm>

namespace rr {

CaptureFrameWindow::CaptureFrameWindow(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
}

void CaptureFrameWindow::beginEditing(const CaptureRegion& initial) {
    recording_ = false;
    setWindowFlag(Qt::WindowTransparentForInput, false);   // editing mode needs to receive mouse events
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setRegionGeometry(initial);
    updateInputMask();        // center hole is click-through, surrounding ring is grabbable
    show();
    raise();
    setCursor(Qt::SizeAllCursor);
    update();
}

void CaptureFrameWindow::enterRecordingStyle() {
    recording_ = true;
    // Make the whole window genuinely click-through (clear the X11 input shape): the application
    // beneath stays fully operable while recording.
    clearMask();
    setWindowFlag(Qt::WindowTransparentForInput, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    unsetCursor();
    show();          // must show again after changing window flags
    raise();
    update();
}

void CaptureFrameWindow::setRegionGeometry(const CaptureRegion& r) {
    setGeometry(frameGeomForRegion(r, border_));
}

CaptureRegion CaptureFrameWindow::captureRegion() const {
    return holeRegionFromFrame(geometry(), border_);
}

void CaptureFrameWindow::updateInputMask() {
    if (recording_) { clearMask(); return; }
    const int r = std::min({inputRing_, width() / 2 - 1, height() / 2 - 1});
    if (r <= 0) { clearMask(); return; }
    QRegion ring(rect());
    ring -= QRegion(rect().adjusted(r, r, -r, -r));   // cut out the center hole -> center becomes click-through
    setMask(ring);
}

void CaptureFrameWindow::resizeEvent(QResizeEvent*) {
    if (!recording_) updateInputMask();
}

int CaptureFrameWindow::hitTest(const QPoint& pos) const {
    const int w = width(), h = height();
    int e = None;
    if (pos.x() <= grip_) e |= L;
    else if (pos.x() >= w - grip_) e |= R;
    if (pos.y() <= grip_) e |= T;
    else if (pos.y() >= h - grip_) e |= B;
    return e;
}

void CaptureFrameWindow::applyCursor(int edges) {
    switch (edges) {
        case L: case R: setCursor(Qt::SizeHorCursor); break;
        case T: case B: setCursor(Qt::SizeVerCursor); break;
        case L | T: case R | B: setCursor(Qt::SizeFDiagCursor); break;
        case R | T: case L | B: setCursor(Qt::SizeBDiagCursor); break;
        default: setCursor(Qt::SizeAllCursor); break;
    }
}

void CaptureFrameWindow::mousePressEvent(QMouseEvent* e) {
    if (recording_ || e->button() != Qt::LeftButton) return;
    active_ = true;
    activeEdges_ = hitTest(e->pos());
    pressGlobal_ = e->globalPosition().toPoint();
    pressGeom_ = geometry();
}

void CaptureFrameWindow::mouseMoveEvent(QMouseEvent* e) {
    if (recording_) return;
    if (!active_) { applyCursor(hitTest(e->pos())); return; }

    const QPoint d = e->globalPosition().toPoint() - pressGlobal_;
    QRect g = pressGeom_;
    const int minW = 80, minH = 60;

    if (activeEdges_ == None) {
        g.moveTopLeft(pressGeom_.topLeft() + d);
    } else {
        if (activeEdges_ & L) g.setLeft(std::min(g.left() + d.x(), g.right() - minW));
        if (activeEdges_ & R) g.setRight(std::max(g.right() + d.x(), g.left() + minW));
        if (activeEdges_ & T) g.setTop(std::min(g.top() + d.y(), g.bottom() - minH));
        if (activeEdges_ & B) g.setBottom(std::max(g.bottom() + d.y(), g.top() + minH));
    }
    setGeometry(g);
    update();
}

void CaptureFrameWindow::mouseReleaseEvent(QMouseEvent*) {
    active_ = false;
}

void CaptureFrameWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);

    if (recording_) {
        // Recording mode: just a thin red border as an indicator, outside the recorded hole (not
        // captured). Never draw anything inside the hole.
        p.setRenderHint(QPainter::Antialiasing, false);
        const QColor red(0xe5, 0x48, 0x4d);
        p.fillRect(rect(), red);
        const QRect hole(border_, border_, width() - 2 * border_, height() - 2 * border_);
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(hole, Qt::transparent);
        return;   // don't draw the size text (otherwise x11grab would record it into the video's top-left corner)
    }

    // Editing mode: visible semi-transparent grab ring (hints draggable/resizable) + thin border +
    // corner markers + size readout.
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent(0x19, 0xc3, 0xa3);
    const int r = std::min({inputRing_, width() / 2 - 1, height() / 2 - 1});

    // Grab ring: fill the outer rectangle with the semi-transparent accent color, clear the inner click-through hole
    QColor ringFill = accent; ringFill.setAlpha(36);
    p.fillRect(rect(), ringFill);
    const QRect inner = rect().adjusted(r, r, -r, -r);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(inner, Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Thin border
    QPen pen(accent); pen.setWidth(2);
    p.setPen(pen);
    p.drawRect(rect().adjusted(1, 1, -1, -1));

    // Short corner markers to reinforce the "resizable" hint
    p.setPen(QPen(accent, 3));
    const int g = 14;
    const int x0 = 2, y0 = 2, x1 = width() - 3, y1 = height() - 3;
    p.drawLine(x0, y0, x0 + g, y0);  p.drawLine(x0, y0, x0, y0 + g);
    p.drawLine(x1, y0, x1 - g, y0);  p.drawLine(x1, y0, x1, y0 + g);
    p.drawLine(x0, y1, x0 + g, y1);  p.drawLine(x0, y1, x0, y1 - g);
    p.drawLine(x1, y1, x1 - g, y1);  p.drawLine(x1, y1, x1, y1 - g);

    // Size readout (a solid pill centered at the top, drawn inside the grab ring and outside the
    // click-through hole; shown in editing mode, not captured)
    const CaptureRegion cr = captureRegion();
    QFont f = p.font();
    if (f.pointSizeF() > 0) f.setPointSizeF(f.pointSizeF() + 1);
    else if (f.pixelSize() > 0) f.setPixelSize(f.pixelSize() + 2);
    f.setBold(true);
    p.setFont(f);
    const QString dim = QStringLiteral("%1 × %2").arg(cr.w).arg(cr.h);
    const QFontMetrics fm(f);
    const int pw = fm.horizontalAdvance(dim) + 16, ph = fm.height() + 4;
    const QRect pill((width() - pw) / 2, 4, pw, ph);
    if (pill.bottom() < inner.top()) {   // only draw the pill when it sits above the click-through hole, to avoid covering it
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(pill, ph / 2.0, ph / 2.0);
        p.setPen(QColor(0x06, 0x12, 0x0f));
        p.drawText(pill, Qt::AlignCenter, dim);
    }
}

}
