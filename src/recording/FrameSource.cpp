#include "recording/FrameSource.h"

#include <QByteArray>
#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "recording/WaylandFrameSource.h"
#include "recording/X11FrameSource.h"
#elif defined(Q_OS_WIN)
#include "recording/WindowsFrameSource.h"
#elif defined(Q_OS_MAC)
#include "recording/MacFrameSource.h"
#endif

namespace rr {

std::unique_ptr<FrameSource> createFrameSource() {
#if defined(Q_OS_LINUX)
    // Native Wayland sessions: X11 grabbing reads back black for Wayland windows
    // (XShm only sees XWayland), so use the xdg-desktop-portal + PipeWire backend.
    // X11 sessions (and pure XWayland with no compositor PipeWire) keep XShm.
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
        qgetenv("XDG_SESSION_TYPE") == "wayland") {
        return std::make_unique<WaylandFrameSource>();
    }
    return std::make_unique<X11FrameSource>();
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowsFrameSource>();
#elif defined(Q_OS_MAC)
    // Quartz CGDisplay capture (main display). Requires Screen Recording permission.
    return std::make_unique<MacFrameSource>();
#else
    return nullptr;
#endif
}

}  // namespace rr
