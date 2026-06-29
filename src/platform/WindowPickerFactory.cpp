#include "platform/WindowPicker.h"

#include <QtGlobal>

#if defined(Q_OS_LINUX)
#include "platform/WindowPickerX11.h"
#endif

namespace rr {

std::unique_ptr<WindowPicker> createWindowPicker() {
#if defined(Q_OS_LINUX)
    return std::make_unique<WindowPickerX11>();
#else
    // Windows/macOS pickers land in later phases.
    return nullptr;
#endif
}

}  // namespace rr
