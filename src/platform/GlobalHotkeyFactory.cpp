#include "platform/GlobalHotkey.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/GlobalHotkeyX11.h"
#endif

namespace rr {

GlobalHotkey* createGlobalHotkey(QObject* parent) {
#if defined(Q_OS_LINUX)
    return new GlobalHotkeyX11(parent);
#else
    // Windows/macOS backends land in later phases; no global hotkey for now.
    // The always-available floating Stop HUD remains the reliable stop path.
    (void)parent;
    return nullptr;
#endif
}

}  // namespace rr
