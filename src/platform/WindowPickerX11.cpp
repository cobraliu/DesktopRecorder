#include "platform/WindowPickerX11.h"

// Qt headers must precede Xlib: X11 defines Status/Bool/None macros that
// break the Qt headers if they are seen first.
#include <QGuiApplication>
#include <QScreen>

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <cmath>

namespace rr {

static int evenDown(int v) { return v - (v % 2); }

bool WindowPickerX11::pickBlocking(CaptureRegion& out) {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return false;
    const Window root = DefaultRootWindow(d);
    const Cursor cur = XCreateFontCursor(d, XC_crosshair);

    if (XGrabPointer(d, root, False, ButtonPressMask,
                     GrabModeAsync, GrabModeAsync, None, cur, CurrentTime) != GrabSuccess) {
        XFreeCursor(d, cur);
        XCloseDisplay(d);
        return false;
    }
    XGrabKeyboard(d, root, False, GrabModeAsync, GrabModeAsync, CurrentTime); // capture Esc

    bool ok = false;
    XEvent ev;
    for (;;) {
        XNextEvent(d, &ev);
        if (ev.type == KeyPress) {
            if (XLookupKeysym(&ev.xkey, 0) == XK_Escape) { ok = false; break; }
        } else if (ev.type == ButtonPress) {
            Window target = ev.xbutton.subwindow ? ev.xbutton.subwindow : ev.xbutton.window;
            if (target == None) target = root;

            Window rootRet = 0, child = 0;
            int gx = 0, gy = 0, ax = 0, ay = 0;
            unsigned int gw = 0, gh = 0, bw = 0, depth = 0;
            if (XGetGeometry(d, target, &rootRet, &gx, &gy, &gw, &gh, &bw, &depth)) {
                XTranslateCoordinates(d, target, root, 0, 0, &ax, &ay, &child);
                const int sw = DisplayWidth(d, DefaultScreen(d));
                const int sh = DisplayHeight(d, DefaultScreen(d));
                int x = ax < 0 ? 0 : ax;
                int y = ay < 0 ? 0 : ay;
                int w = static_cast<int>(gw);
                int h = static_cast<int>(gh);
                if (x + w > sw) w = sw - x;
                if (y + h > sh) h = sh - y;
                // XGetGeometry reports physical pixels while the caller (the
                // floating frame) works in Qt logical coordinates; scale down
                // by the device pixel ratio (global on X11).
                const QScreen* qs = QGuiApplication::primaryScreen();
                const double dpr = qs ? qs->devicePixelRatio() : 1.0;
                out.x = static_cast<int>(std::lround(x / dpr));
                out.y = static_cast<int>(std::lround(y / dpr));
                out.w = evenDown(static_cast<int>(std::lround(w / dpr)));
                out.h = evenDown(static_cast<int>(std::lround(h / dpr)));
                ok = (out.w >= 2 && out.h >= 2);
            }
            break;
        }
    }

    XUngrabKeyboard(d, CurrentTime);
    XUngrabPointer(d, CurrentTime);
    XFreeCursor(d, cur);
    XSync(d, False);
    XCloseDisplay(d);
    return ok;
}

}
