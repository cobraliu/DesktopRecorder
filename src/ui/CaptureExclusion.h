#pragma once

class QWidget;

namespace rr {

// Exclude a top-level window from OS screen capture so the application's own
// overlays (the Stop HUD, the recording frame, the countdown) are not recorded
// into the output video.
//
// Windows: SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE) — the window
// stays visible to the user but is omitted from GDI BitBlt / DWM captures.
// Because the affinity is a property of the native HWND, callers must re-apply
// this after any window-flag change that can recreate the native handle.
//
// No-op on other platforms (X11/Wayland avoid self-capture by other means;
// macOS is handled at the capture-source level).
void excludeFromScreenCapture(QWidget* w);

}  // namespace rr
