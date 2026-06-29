#include "recording/types.h"

namespace rr {

bool isValidRegion(const CaptureRegion& r) {
    return r.w > 0 && r.h > 0 && (r.w % 2 == 0) && (r.h % 2 == 0);
}

CaptureRegion normalizeRegion(CaptureRegion r) {
    if (r.w < 0) { r.x += r.w; r.w = -r.w; }
    if (r.h < 0) { r.y += r.h; r.h = -r.h; }
    r.w -= (r.w % 2);
    r.h -= (r.h % 2);
    return r;
}

}
