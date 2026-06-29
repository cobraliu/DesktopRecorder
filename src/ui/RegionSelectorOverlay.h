#pragma once
#include <QWidget>
#include <QPoint>
#include <QRect>
#include "recording/types.h"
namespace rr {
class RegionSelectorOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelectorOverlay(QWidget* parent = nullptr);
    void beginSelection();
signals:
    void regionSelected(const rr::CaptureRegion& region);
    void cancelled();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPoint origin_;
    QRect rubber_;
    bool dragging_ = false;
};
}
