#pragma once
#include <string>

namespace rr {

struct CaptureRegion {
    // x/y/w/h are in Qt logical coordinates; frame sources that address the
    // screen in physical pixels (X11, Windows) multiply them by dpiScale.
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int screenIndex = 0;
    double dpiScale = 1.0;
};

struct OutputOptions {
    std::string path;
    int fps = 30;
    bool audioEnabled = false;
};

bool isValidRegion(const CaptureRegion& r);
CaptureRegion normalizeRegion(CaptureRegion r);

}
