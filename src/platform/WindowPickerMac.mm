#include "platform/WindowPickerMac.h"

#include <CoreGraphics/CoreGraphics.h>

#include <chrono>
#include <thread>

namespace rr {

namespace {
constexpr CGKeyCode kEscapeKeyCode = 53;  // kVK_Escape, without pulling in Carbon here
int evenDown(int v) { return v - (v % 2); }

// Find the frontmost normal window whose bounds contain p (CG global, top-left origin).
bool hitTestWindow(CGPoint p, CaptureRegion& out) {
    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows) return false;

    bool found = false;
    const CFIndex n = CFArrayGetCount(windows);  // front-to-back order
    for (CFIndex i = 0; i < n; ++i) {
        auto w = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
        if (!w) continue;

        // Skip non-zero layers (menu bar, dock, status items); keep normal app windows.
        int layer = 0;
        if (auto layerNum = static_cast<CFNumberRef>(CFDictionaryGetValue(w, kCGWindowLayer)))
            CFNumberGetValue(layerNum, kCFNumberIntType, &layer);
        if (layer != 0) continue;

        // Skip fully transparent windows: invisible overlays sit at layer 0 and
        // would otherwise win the hit test over the window the user can see.
        double alpha = 1.0;
        if (auto alphaNum = static_cast<CFNumberRef>(CFDictionaryGetValue(w, kCGWindowAlpha)))
            CFNumberGetValue(alphaNum, kCFNumberDoubleType, &alpha);
        if (alpha <= 0.0) continue;

        auto boundsDict = static_cast<CFDictionaryRef>(CFDictionaryGetValue(w, kCGWindowBounds));
        if (!boundsDict) continue;
        CGRect r;
        if (!CGRectMakeWithDictionaryRepresentation(boundsDict, &r)) continue;

        if (CGRectContainsPoint(r, p)) {
            const int ww = evenDown(static_cast<int>(r.size.width));
            const int hh = evenDown(static_cast<int>(r.size.height));
            if (ww >= 2 && hh >= 2) {
                out.x = static_cast<int>(r.origin.x);
                out.y = static_cast<int>(r.origin.y);
                out.w = ww;
                out.h = hh;
                found = true;
            }
            break;  // frontmost window under the cursor wins
        }
    }

    CFRelease(windows);
    return found;
}
}  // namespace

bool WindowPickerMac::pickBlocking(CaptureRegion& out) {
    for (;;) {
        if (CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, kEscapeKeyCode))
            return false;

        if (CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft)) {
            CGEventRef e = CGEventCreate(nullptr);
            const CGPoint p = CGEventGetLocation(e);  // global, top-left origin (matches CGWindowBounds)
            if (e) CFRelease(e);

            const bool ok = hitTestWindow(p, out);
            // Wait for release so the click does not keep registering.
            while (CGEventSourceButtonState(kCGEventSourceStateHIDSystemState, kCGMouseButtonLeft))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return ok;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace rr
