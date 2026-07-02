#pragma once
#include <QWidget>
class QTimer;
class QScreen;
namespace rr {
class CountdownOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CountdownOverlay(QWidget* parent = nullptr);
    // screen: where to show the overlay (the screen being recorded); primary if null.
    void start(int seconds, QScreen* screen = nullptr);
    void cancel();                 // abort a running countdown without emitting countdownFinished
    bool isCounting() const;
    // Whether the global stop hotkey actually registered; decides the hint text.
    void setHotkeyAvailable(bool available);
signals:
    void countdownFinished();
protected:
    void paintEvent(QPaintEvent*) override;
private slots:
    void tick();
private:
    int remaining_ = 0;
    bool hotkeyAvailable_ = true;
    QTimer* timer_ = nullptr;
};
}
