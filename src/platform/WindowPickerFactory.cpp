#include "platform/WindowPicker.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/WindowPickerX11.h"
#elif defined(Q_OS_WIN)
#include "platform/WindowPickerWindows.h"
#endif

namespace rr {

std::unique_ptr<WindowPicker> createWindowPicker() {
#if defined(Q_OS_LINUX)
    return std::make_unique<WindowPickerX11>();
#elif defined(Q_OS_WIN)
    return std::make_unique<WindowPickerWindows>();
#else
    // macOS picker lands in a later phase.
    return nullptr;
#endif
}

}  // namespace rr
