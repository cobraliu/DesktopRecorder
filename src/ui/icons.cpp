#include "ui/icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QRadialGradient>

namespace rr {

// Draw on a transparent canvas of the given logical size, returning a QPixmap with the correct devicePixelRatio.
template <typename Fn>
static QPixmap paintIcon(int px, Fn&& draw) {
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    draw(p, QRectF(0, 0, px, px));
    p.end();
    return pm;
}

static QPen strokePen(const QColor& c, qreal w = 1.6) {
    QPen pen(c);
    pen.setWidthF(w);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

QIcon iconFullscreen(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(strokePen(c));
        QRectF body = r.adjusted(r.width() * 0.14, r.height() * 0.20,
                                 -r.width() * 0.14, -r.height() * 0.30);
        p.drawRoundedRect(body, 2, 2);
        const qreal cy = r.bottom() - r.height() * 0.12;
        p.drawLine(QPointF(r.center().x(), body.bottom()),
                   QPointF(r.center().x(), cy));
        p.drawLine(QPointF(r.width() * 0.32, cy), QPointF(r.width() * 0.68, cy));
    }));
}

QIcon iconFrame(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(strokePen(c));
        const qreal m = r.width() * 0.18, len = r.width() * 0.22;
        const qreal l = r.left() + m, t = r.top() + m,
                    rr = r.right() - m, b = r.bottom() - m;
        // Four corner brackets
        p.drawLine(QPointF(l, t), QPointF(l + len, t));
        p.drawLine(QPointF(l, t), QPointF(l, t + len));
        p.drawLine(QPointF(rr, t), QPointF(rr - len, t));
        p.drawLine(QPointF(rr, t), QPointF(rr, t + len));
        p.drawLine(QPointF(l, b), QPointF(l + len, b));
        p.drawLine(QPointF(l, b), QPointF(l, b - len));
        p.drawLine(QPointF(rr, b), QPointF(rr - len, b));
        p.drawLine(QPointF(rr, b), QPointF(rr, b - len));
    }));
}

QIcon iconWindow(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(strokePen(c));
        QRectF body = r.adjusted(r.width() * 0.16, r.height() * 0.18,
                                 -r.width() * 0.16, -r.height() * 0.18);
        p.drawRoundedRect(body, 2.5, 2.5);
        const qreal ty = body.top() + body.height() * 0.28;
        p.drawLine(QPointF(body.left(), ty), QPointF(body.right(), ty));
    }));
}

QIcon iconPlay(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        QPainterPath tri;
        tri.moveTo(r.width() * 0.34, r.height() * 0.26);
        tri.lineTo(r.width() * 0.34, r.height() * 0.74);
        tri.lineTo(r.width() * 0.76, r.height() * 0.50);
        tri.closeSubpath();
        p.drawPath(tri);
    }));
}

QIcon iconTrash(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(strokePen(c, 1.5));
        const qreal w = r.width(), h = r.height();
        // Lid
        p.drawLine(QPointF(w * 0.22, h * 0.28), QPointF(w * 0.78, h * 0.28));
        // Handle
        p.drawLine(QPointF(w * 0.40, h * 0.28), QPointF(w * 0.40, h * 0.20));
        p.drawLine(QPointF(w * 0.40, h * 0.20), QPointF(w * 0.60, h * 0.20));
        p.drawLine(QPointF(w * 0.60, h * 0.20), QPointF(w * 0.60, h * 0.28));
        // Can body
        QPainterPath can;
        can.moveTo(w * 0.28, h * 0.28);
        can.lineTo(w * 0.32, h * 0.80);
        can.lineTo(w * 0.68, h * 0.80);
        can.lineTo(w * 0.72, h * 0.28);
        p.drawPath(can);
        // Vertical ridges
        p.drawLine(QPointF(w * 0.44, h * 0.40), QPointF(w * 0.45, h * 0.70));
        p.drawLine(QPointF(w * 0.56, h * 0.40), QPointF(w * 0.55, h * 0.70));
    }));
}

QIcon iconRecord(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal d = r.width() * 0.46;
        p.drawEllipse(r.center(), d / 2, d / 2);
    }));
}

QIcon iconStop(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal s = r.width() * 0.5;
        QRectF sq(r.center().x() - s / 2, r.center().y() - s / 2, s, s);
        p.drawRoundedRect(sq, 2, 2);
    }));
}

QIcon iconFolder(const QColor& c, int px) {
    return QIcon(paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(strokePen(c, 1.5));
        const qreal w = r.width(), h = r.height();
        const qreal l = w * 0.18, rr = w * 0.82;
        const qreal top = h * 0.30, bot = h * 0.78;
        // Tab flap + main body
        QPainterPath body;
        body.moveTo(l, top);
        body.lineTo(w * 0.40, top);
        body.lineTo(w * 0.48, top + h * 0.10);
        body.lineTo(rr, top + h * 0.10);
        body.lineTo(rr, bot);
        body.lineTo(l, bot);
        body.closeSubpath();
        p.drawPath(body);
    }));
}

QPixmap statusDot(const QColor& c, int px) {
    return paintIcon(px, [&](QPainter& p, const QRectF& r) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        const qreal d = r.width() * 0.7;
        p.drawEllipse(r.center(), d / 2, d / 2);
    });
}

QImage renderAppIcon(int px) {
    // Proportions mirror packaging/icon/RegionRecord.svg (authored on a 1024 grid),
    // expressed as fractions of px so every export size stays identical.
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = px;                 // shorthand
    const QRectF box(0, 0, s, s);
    const qreal radius = s * 0.225;

    // Background squircle with a top-to-bottom dark gradient.
    QLinearGradient bg(0, 0, 0, s);
    bg.setColorAt(0.0, QColor(0x1c, 0x22, 0x2b));
    bg.setColorAt(1.0, QColor(0x0e, 0x12, 0x17));
    QPainterPath bgPath;
    bgPath.addRoundedRect(box, radius, radius);
    p.fillPath(bgPath, bg);

    // Faint teal edge highlight for depth.
    {
        QPen edge(QColor(0x2b, 0xd4, 0xb5));
        edge.setWidthF(s * 0.003);
        QColor ec = edge.color();
        ec.setAlphaF(0.10);
        edge.setColor(ec);
        p.setPen(edge);
        p.setBrush(Qt::NoBrush);
        const qreal h = edge.widthF() / 2.0;
        p.drawRoundedRect(box.adjusted(h, h, -h, -h), radius, radius);
    }

    // Region-selection corner brackets (teal), as four rounded polylines.
    {
        QPen pen(QColor(0x19, 0xc3, 0xa3));
        pen.setWidthF(s * 0.0508);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal lo = s * 0.281, hi = s * 0.719, arm = s * 0.156;
        auto poly = [&](QPointF a, QPointF b, QPointF c) {
            QPainterPath path;
            path.moveTo(a);
            path.lineTo(b);
            path.lineTo(c);
            p.drawPath(path);
        };
        poly({lo, lo + arm}, {lo, lo}, {lo + arm, lo});           // top-left
        poly({hi - arm, lo}, {hi, lo}, {hi, lo + arm});           // top-right
        poly({lo, hi - arm}, {lo, hi}, {lo + arm, hi});           // bottom-left
        poly({hi, hi - arm}, {hi, hi}, {hi - arm, hi});           // bottom-right
    }

    // Record dot (red) with a soft radial glow.
    {
        const QPointF c = box.center();
        QRadialGradient glow(c, s * 0.174);
        glow.setColorAt(0.0, QColor(0xff, 0x5a, 0x60, 140));
        glow.setColorAt(0.6, QColor(0xff, 0x5a, 0x60, 46));
        glow.setColorAt(1.0, QColor(0xff, 0x5a, 0x60, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, s * 0.174, s * 0.174);

        QLinearGradient dot(c.x(), c.y() - s * 0.117, c.x(), c.y() + s * 0.117);
        dot.setColorAt(0.0, QColor(0xff, 0x5c, 0x61));
        dot.setColorAt(1.0, QColor(0xe2, 0x3b, 0x41));
        p.setBrush(dot);
        p.drawEllipse(c, s * 0.117, s * 0.117);
    }

    p.end();
    return img;
}

QIcon appIcon() {
    QIcon icon;
    for (int px : {16, 24, 32, 48, 64, 128, 256})
        icon.addPixmap(QPixmap::fromImage(renderAppIcon(px)));
    return icon;
}

}
