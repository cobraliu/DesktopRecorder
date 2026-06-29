#include "ui/frame_geometry.h"

namespace rr {

static int evenDown(int v) { return v - (v % 2); }

CaptureRegion holeRegionFromFrame(const QRect& frameGeom, int border) {
    CaptureRegion r;
    r.x = frameGeom.x() + border;
    r.y = frameGeom.y() + border;
    int w = frameGeom.width() - 2 * border;
    int h = frameGeom.height() - 2 * border;
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    r.w = evenDown(w);
    r.h = evenDown(h);
    return r;
}

QRect frameGeomForRegion(const CaptureRegion& r, int border) {
    return QRect(r.x - border, r.y - border,
                 r.w + 2 * border, r.h + 2 * border);
}

}
