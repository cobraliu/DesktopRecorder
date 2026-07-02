#pragma once
#include <QWidget>
class QTimer;
namespace rr {
class CountdownOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CountdownOverlay(QWidget* parent = nullptr);
    void start(int seconds);
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
