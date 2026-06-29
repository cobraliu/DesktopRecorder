#include "recording/FrameSource.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "recording/X11FrameSource.h"
// WaylandFrameSource (xdg-desktop-portal + PipeWire) lands in Phase 2; this factory
// will then branch to it when XDG_SESSION_TYPE==wayland / WAYLAND_DISPLAY is set.
#elif defined(Q_OS_WIN)
#include "recording/WindowsFrameSource.h"
#endif

namespace rr {

std::unique_ptr<FrameSource> createFrameSource() {
#if defined(Q_OS_LINUX)
    // X11 capture works on native X sessions and under XWayland. Native Wayland
    // windows read back black via XShm; the Phase 2 Wayland backend handles those.
    return std::make_unique<X11FrameSource>();
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowsFrameSource>();
#elif defined(Q_OS_MAC)
    return nullptr;  // MacFrameSource: Phase 4
#else
    return nullptr;
#endif
}

}  // namespace rr
