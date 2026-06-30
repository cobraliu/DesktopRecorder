#pragma once
#include "platform/WindowPicker.h"

namespace rr {

// macOS window picker: waits for a left click, then returns the frontmost normal
// (layer-0) on-screen window whose bounds contain the click point. Esc cancels.
class WindowPickerMac : public WindowPicker {
public:
    bool pickBlocking(CaptureRegion& out) override;
};

}  // namespace rr
