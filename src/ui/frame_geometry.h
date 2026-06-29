#pragma once
#include <QRect>
#include "recording/types.h"

namespace rr {

// Floating frame's outer geometry + border thickness -> recorded region (the transparent center
// hole). Width and height are rounded to even values, minimum 2.
CaptureRegion holeRegionFromFrame(const QRect& frameGeom, int border);

// Recorded region + border thickness -> floating frame's outer geometry (the hole expanded outward
// by border on all four sides).
QRect frameGeomForRegion(const CaptureRegion& r, int border);

}
