#include "platform/GlobalHotkey.h"

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
