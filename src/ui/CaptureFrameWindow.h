#pragma once
#include <QWidget>
#include <QRect>
#include <QPoint>
#include "recording/types.h"

namespace rr {

// Peek-style semi-transparent capture frame: the truly transparent center hole = the recorded
// region, with a visible border around it.
// Editing mode: teal border, draggable to move, resizable by edges/corners.
// Recording mode: red border, click-through; serves only as a "recording this area" indicator and,
// because the hole is transparent, is not captured.
class CaptureFrameWindow : public QWidget {
    Q_OBJECT
public:
    explicit CaptureFrameWindow(QWidget* parent = nullptr);

    void beginEditing(const CaptureRegion& initial);  // show and enter editing mode
    void enterRecordingStyle();                        // switch to red + click-through
    void setRegionGeometry(const CaptureRegion& r);    // position the outer frame by region
    CaptureRegion captureRegion() const;               // the current hole's region

    int border() const { return border_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    enum Edge { None = 0, L = 1, R = 2, T = 4, B = 8 };
    int  hitTest(const QPoint& pos) const;   // returns an Edge bit-or; 0 means the move area
    void applyCursor(int edges);
    void updateInputMask();    // editing mode: carve the center hole click-through, leaving only the surrounding grab ring

    int   border_ = 4;         // visible border thickness (also the recorded region's inset from the outer frame)
    int   grip_ = 12;          // edge-resize hit width
    int   inputRing_ = 28;     // thickness of the interactive (move/resize) ring in editing mode; inside it is the click-through hole
    bool  recording_ = false;

    bool  active_ = false;     // currently dragging/resizing
    int   activeEdges_ = 0;    // 0 = move
    QPoint pressGlobal_;
    QRect  pressGeom_;
};

}
