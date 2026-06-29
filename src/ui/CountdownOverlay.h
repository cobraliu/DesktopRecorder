#pragma once
#include <QWidget>
class QTimer;
namespace rr {
class CountdownOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CountdownOverlay(QWidget* parent = nullptr);
    void start(int seconds);
signals:
    void countdownFinished();
protected:
    void paintEvent(QPaintEvent*) override;
private slots:
    void tick();
private:
    int remaining_ = 0;
    QTimer* timer_ = nullptr;
};
}
