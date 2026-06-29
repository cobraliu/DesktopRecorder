#include "app/RegionPresets.h"
#include <algorithm>

namespace rr {

static int evenDown(int v) { return v - (v % 2); }

QVector<Preset> standardPresets() {
    return {
        {QStringLiteral("720p (1280×720)"),  1280, 720},
        {QStringLiteral("1080p (1920×1080)"), 1920, 1080},
        {QStringLiteral("16:9 (960×540)"),    960, 540},
        {QStringLiteral("4:3 (800×600)"),     800, 600},
        {QStringLiteral("1:1 (720×720)"),     720, 720},
    };
}

CaptureRegion regionFromPreset(const Preset& p,
                               int screenX, int screenY, int screenW, int screenH) {
    CaptureRegion r;
    r.w = evenDown(std::min(p.w, screenW));
    r.h = evenDown(std::min(p.h, screenH));
    r.x = screenX + (screenW - r.w) / 2;
    r.y = screenY + (screenH - r.h) / 2;
    return r;
}

CaptureRegion fullscreenRegion(int screenX, int screenY, int screenW, int screenH) {
    CaptureRegion r;
    r.x = screenX; r.y = screenY;
    r.w = evenDown(screenW);
    r.h = evenDown(screenH);
    return r;
}

}
