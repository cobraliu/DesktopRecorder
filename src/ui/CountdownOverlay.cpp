#include "ui/CountdownOverlay.h"
#include "ui/CaptureExclusion.h"
#include <QTimer>
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>

namespace rr {

CountdownOverlay::CountdownOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &CountdownOverlay::tick);
}

void CountdownOverlay::setHotkeyAvailable(bool available) {
    hotkeyAvailable_ = available;
}

void CountdownOverlay::start(int seconds) {
    if (seconds <= 0) { emit countdownFinished(); return; }
    remaining_ = seconds;
    if (QScreen* s = QGuiApplication::primaryScreen())
        setGeometry(s->geometry());
#if defined(Q_OS_MACOS)
    // showFullScreen() on macOS enters a native fullscreen Space and renders this translucent
    // Tool window as an opaque black surface (which can also linger into the recording). Show it
    // as a plain borderless top-level sized to the screen instead, so the dimming stays
    // translucent. X11/Windows keep showFullScreen(), where it is verified to work.
    show();
    raise();
#else
    showFullScreen();
#endif
    // Don't let the countdown overlay leak into an in-progress recording (Windows).
    excludeFromScreenCapture(this);
    update();
    timer_->start();
}

void CountdownOverlay::tick() {
    if (--remaining_ <= 0) {
        timer_->stop();
        hide();
        emit countdownFinished();
        return;
    }
    update();
}

void CountdownOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    QFont f = p.font(); f.setPointSize(160); f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, QString::number(remaining_));
    QFont sf = p.font(); sf.setPointSize(24); sf.setBold(false);
    p.setFont(sf);
    QRect hint = rect(); hint.setTop(rect().center().y() + 140);
    p.drawText(hint, Qt::AlignHCenter | Qt::AlignTop,
               hotkeyAvailable_
                   ? QStringLiteral("Press Ctrl+Alt+S to stop recording")
                   : QStringLiteral("Use the floating Stop button to stop recording"));
}

}
