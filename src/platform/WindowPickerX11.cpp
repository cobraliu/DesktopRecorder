#include "platform/WindowPickerX11.h"

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

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
                out.x = x;
                out.y = y;
                out.w = evenDown(w);
                out.h = evenDown(h);
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
