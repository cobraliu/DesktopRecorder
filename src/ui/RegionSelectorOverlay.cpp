#include "ui/RegionSelectorOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>

namespace rr {

RegionSelectorOverlay::RegionSelectorOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
}

void RegionSelectorOverlay::beginSelection() {
    if (QScreen* s = QGuiApplication::primaryScreen())
        setGeometry(s->geometry());
    rubber_ = QRect();
    showFullScreen();
}

void RegionSelectorOverlay::mousePressEvent(QMouseEvent* e) {
    origin_ = e->pos(); rubber_ = QRect(origin_, QSize()); dragging_ = true; update();
}
void RegionSelectorOverlay::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) { rubber_ = QRect(origin_, e->pos()).normalized(); update(); }
}
void RegionSelectorOverlay::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = false;
    const QRect r = rubber_.normalized();
    hide();
    if (r.width() >= 2 && r.height() >= 2) {
        CaptureRegion reg;
        reg.x = x() + r.x(); reg.y = y() + r.y();
        reg.w = r.width() - (r.width() % 2);
        reg.h = r.height() - (r.height() % 2);
        emit regionSelected(reg);
    } else {
        emit cancelled();
    }
}
void RegionSelectorOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { hide(); emit cancelled(); }
}
void RegionSelectorOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 80));
    if (!rubber_.isNull()) {
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(rubber_, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(Qt::red, 2));
        p.drawRect(rubber_);
        p.setPen(Qt::white);
        p.drawText(rubber_.topLeft() + QPoint(4, -6),
                   QString("%1×%2").arg(rubber_.width()).arg(rubber_.height()));
    }
}

}
