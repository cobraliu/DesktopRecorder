#include "platform/GlobalHotkey.h"

#include <QByteArray>
#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/GlobalHotkeyX11.h"
#elif defined(Q_OS_WIN)
#include "platform/GlobalHotkeyWindows.h"
#elif defined(Q_OS_MAC)
#include "platform/GlobalHotkeyMac.h"
#endif

namespace rr {

GlobalHotkey* createGlobalHotkey(QObject* parent) {
#if defined(Q_OS_LINUX)
    // On native Wayland sessions XGrabKey on the XWayland root registers
    // "successfully" but never fires while a Wayland window has focus, so the
    // hotkey would silently do nothing while the UI advertises it. Report no
    // backend instead; the floating Stop HUD remains the stop path. Same
    // session check as createFrameSource().
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        (void)parent;
        return nullptr;
    }
    return new GlobalHotkeyX11(parent);
#elif defined(Q_OS_WIN)
    return new GlobalHotkeyWindows(parent);
#elif defined(Q_OS_MAC)
    return new GlobalHotkeyMac(parent);
#else
    // No global-hotkey backend on this platform; the always-available floating
    // Stop HUD remains the reliable stop path.
    (void)parent;
    return nullptr;
#endif
}

}  // namespace rr
