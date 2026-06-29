#pragma once
#include <QString>
#include <QVector>
#include "recording/types.h"

namespace rr {

struct Preset { QString name; int w; int h; };

QVector<Preset> standardPresets();
CaptureRegion regionFromPreset(const Preset& p,
                               int screenX, int screenY, int screenW, int screenH);
CaptureRegion fullscreenRegion(int screenX, int screenY, int screenW, int screenH);

}
