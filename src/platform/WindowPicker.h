#pragma once
#include <memory>
#include "recording/types.h"

namespace rr {

// Interactive window picker: lets the user click a window and returns its screen rectangle.
// Blocks until a click or until the user cancels. Returns false on cancel/failure.
class WindowPicker {
public:
    virtual ~WindowPicker() = default;
    virtual bool pickBlocking(CaptureRegion& out) = 0;
};

// Creates the window picker for the current platform.
// Returns nullptr on platforms without a backend yet (callers must null-check).
std::unique_ptr<WindowPicker> createWindowPicker();

}  // namespace rr
