#include "platform/WindowPicker.h"

#include <QByteArray>
#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/WindowPickerX11.h"
#elif defined(Q_OS_WIN)
#include "platform/WindowPickerWindows.h"
#elif defined(Q_OS_MAC)
#include "platform/WindowPickerMac.h"
#endif

namespace rr {

std::unique_ptr<WindowPicker> createWindowPicker() {
#if defined(Q_OS_LINUX)
    // The X11 picker grabs the pointer and blocks in XNextEvent; on a native
    // Wayland session the grab only covers XWayland clients, so no event ever
    // arrives and the GUI thread would hang forever. Report no picker instead
    // (same session check as createFrameSource()).
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        return nullptr;
    }
    return std::make_unique<WindowPickerX11>();
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowPickerWindows>();
#elif defined(Q_OS_MAC)
    return std::make_unique<WindowPickerMac>();
#else
    return nullptr;
#endif
}

}  // namespace rr
