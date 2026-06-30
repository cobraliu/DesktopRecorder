#pragma once
#include "platform/WindowPicker.h"

namespace rr {

// Windows window picker: shows a crosshair and waits for a left click, then returns
// the clicked top-level window's screen rectangle. Esc cancels.
class WindowPickerWindows : public WindowPicker {
public:
    bool pickBlocking(CaptureRegion& out) override;
};

}  // namespace rr
