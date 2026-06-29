#pragma once
#include "platform/WindowPicker.h"

namespace rr {

// X11 window picker: grabs the pointer + crosshair cursor; after the user clicks a window,
// returns its current screen rectangle. Esc cancels.
class WindowPickerX11 : public WindowPicker {
public:
    bool pickBlocking(CaptureRegion& out) override;
};

}  // namespace rr
