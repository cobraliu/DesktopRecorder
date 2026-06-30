#include "platform/WindowPicker.h"

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
