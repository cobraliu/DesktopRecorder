#include "platform/GlobalHotkeyX11.h"
#include <QThread>

#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace rr {

// Grab error detection: XGrabKey's BadAccess (already taken by another client) is reported
// asynchronously; use a temporary error handler to determine failure after XSync.
static bool g_grabError = false;
static int grabErrorHandler(Display*, XErrorEvent*) { g_grabError = true; return 0; }

GlobalHotkeyX11::GlobalHotkeyX11(QObject* parent) : GlobalHotkey(parent) {
    dpy_ = XOpenDisplay(nullptr);
    if (dpy_) root_ = DefaultRootWindow(static_cast<Display*>(dpy_));
}

GlobalHotkeyX11::~GlobalHotkeyX11() {
    unregisterAll();
    if (dpy_) { XCloseDisplay(static_cast<Display*>(dpy_)); dpy_ = nullptr; }
}

bool GlobalHotkeyX11::registerHotkey(int id, bool ctrl, bool alt, bool shift, int x11keysym) {
    if (!dpy_) return false;
    Display* d = static_cast<Display*>(dpy_);
    unsigned int mods = 0;
    if (ctrl)  mods |= ControlMask;
    if (alt)   mods |= Mod1Mask;
    if (shift) mods |= ShiftMask;
    const KeyCode kc = XKeysymToKeycode(d, static_cast<KeySym>(x11keysym));
    if (kc == 0) return false;

    // owner_events=False: grabbed key events are always reported to grab_window(root), making global hotkeys more reliable.
    // Add common lock states (NumLock=Mod2, CapsLock=LockMask) to ensure they all trigger.
    auto grabOne = [&](unsigned int m) -> bool {
        g_grabError = false;
        XErrorHandler old = XSetErrorHandler(grabErrorHandler);
        XGrabKey(d, kc, m, root_, False, GrabModeAsync, GrabModeAsync);
        XSync(d, False);
        XSetErrorHandler(old);
        return !g_grabError;
    };
    const bool baseOk = grabOne(mods);          // base combination failure = already taken, counted as registration failure
    grabOne(mods | Mod2Mask);
    grabOne(mods | LockMask);
    grabOne(mods | Mod2Mask | LockMask);
    if (!baseOk) return false;

    bindings_.push_back({id, kc, mods});
    if (!thread_) {
        thread_ = QThread::create([this] { eventLoop(); });
        thread_->start();
    }
    return true;
}

void GlobalHotkeyX11::eventLoop() {
    Display* d = static_cast<Display*>(dpy_);
    while (!stop_.load()) {
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == KeyPress) {
                const unsigned int m = ev.xkey.state & (ControlMask | Mod1Mask | ShiftMask);
                for (const auto& b : bindings_)
                    if (b.keycode == ev.xkey.keycode && b.mods == m)
                        emit triggered(b.id);
            }
        }
        QThread::msleep(20);
    }
}

void GlobalHotkeyX11::unregisterAll() {
    stop_.store(true);
    if (thread_) { thread_->quit(); thread_->wait(); delete thread_; thread_ = nullptr; }
    if (dpy_) {
        Display* d = static_cast<Display*>(dpy_);
        XUngrabKey(d, AnyKey, AnyModifier, root_);
        XSync(d, False);
    }
    bindings_.clear();
    stop_.store(false);
}

}
