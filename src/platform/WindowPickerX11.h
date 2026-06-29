#pragma once
#include "recording/types.h"

namespace rr {

// Interactive window picker: grabs the pointer + crosshair cursor; after the user clicks a window, returns its current screen rectangle.
// Blocks until a click or Esc cancels. Returns false on cancel/failure.
class WindowPickerX11 {
public:
    bool pickBlocking(CaptureRegion& out);
};

}
