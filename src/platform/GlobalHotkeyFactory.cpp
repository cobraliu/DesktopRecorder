#include "platform/GlobalHotkey.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/GlobalHotkeyX11.h"
#elif defined(Q_OS_WIN)
#include "platform/GlobalHotkeyWindows.h"
#endif

namespace rr {

GlobalHotkey* createGlobalHotkey(QObject* parent) {
#if defined(Q_OS_LINUX)
    return new GlobalHotkeyX11(parent);
#elif defined(Q_OS_WIN)
    return new GlobalHotkeyWindows(parent);
#else
    // macOS backend lands in a later phase; no global hotkey for now.
    // The always-available floating Stop HUD remains the reliable stop path.
    (void)parent;
    return nullptr;
#endif
}

}  // namespace rr
