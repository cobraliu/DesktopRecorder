# RegionRecord Tech Stack Selection Design

- Date: 2026-06-29
- Status: Confirmed (implementation plan pending)
- Scope: This document only locks in the **tech-stack selection and high-level architecture decisions**, not detailed implementation steps (implementation details are in the subsequent implementation plan).

## 1. Background and Goals

The user has long used [Peek](https://github.com/phw/peek) to record the desktop, but it **frequently hangs when stopping a recording**. Root cause: on stop, Peek **synchronously** waits for the underlying ffmpeg to finalize and transcode to GIF, blocking the UI; furthermore, Peek is no longer maintained.

RegionRecord's goal: build a **cross-platform desktop region-recording tool** that architecturally eliminates the "hang on stop" problem and supports region (rectangular drag-select) recording.

### Success Criteria

- Able to drag-select any rectangular region of the screen and record it to MP4.
- **The UI never hangs after clicking stop**: the recording process finalizes asynchronously, the main thread does not block.
- Supports audio recording: **off by default**, the user can enable it in the UI; when enabled it records the **system default audio input**, multiplexed into the same MP4 as the video.
- The same codebase can build, package, and distribute on Windows, macOS (x86_64 + arm64), and Linux (X11).
- **Single self-contained executable (hard requirement)**: the final artifact depends on no external dynamic libraries (Qt and libav are all statically linked, including Qt platform plugins). See below for the concrete forms.

### Platform Realization of "Single Executable" (precise definition of the hard requirement)

Statically linked Qt + statically linked libav + statically linked Qt platform plugins (`Q_IMPORT_PLUGIN`), loading no external `.dll/.so/.dylib` at runtime.

| Platform | Delivery form | Notes |
|---|---|---|
| Windows | a single `.exe` | a true single file |
| Linux (X11) | a single **AppImage** | the practical-standard "single file"; a purely static ELF with xcb/glibc is too fragile, not adopted |
| macOS | a single `.app` (packed into a `.dmg`) | **system constraint**: a GUI requesting "screen recording" permission must have an Info.plist + signature; a bare binary cannot be granted permission; the `.app` interior is a single executable + plist, with no external dynamic libraries |

All three forms have **zero external dynamic-library dependencies** internally. This is the most "single" form achievable for a GUI screen-recording tool that requests permissions.

### Non-goals (explicitly out of scope for v1)

- **Wayland support** (tackled separately in phase two: PipeWire + xdg-desktop-portal).
- **GIF output** (phase two; v1 does MP4 only to avoid the transcode-hang link).
- Audio **device selection UI** (v1 only uses the system default input; selecting a specific device / multi-track mixing is left to phase two).

## 2. Final Tech Stack

| Layer | Selection | Notes |
|---|---|---|
| Language | **C++** | same language as libav*, integration without FFI friction |
| GUI | **Qt 6** | mature cross-platform GUI; CMake build |
| Recording/encoding | **libav\* statically linked** (`libavformat` / `libavcodec` / `libavdevice` / `libavfilter` / `libavutil` / `libswscale` / `libswresample`) | compiled into a single binary; video H.264 (libx264), audio AAC (ffmpeg's built-in aac), MP4 container |
| Video capture backend | libavdevice input devices | Windows=`gdigrab`, macOS=`avfoundation`, Linux=`x11grab` |
| Audio capture backend (optional, off by default) | libavdevice, system default input device | Windows=`dshow`, macOS=`avfoundation`, Linux=`pulse` |
| UI form | **Qt Widgets** | a tool-type app; good support for frameless translucent windows; no QML |
| Region selection | Qt borderless translucent fullscreen top-most window | drag a box to get `(x, y, w, h)`, uniformly handling multi-monitor/DPI |
| Graceful stop | stop reading frames -> `av_write_trailer` to finalize | runs on a worker thread, UI does not block |
| Build system | **CMake** | |
| Dependency management | **vcpkg** (static triplet) | declare `ffmpeg[x264]`; Qt via install-qt-action or vcpkg |
| CI / distribution | **GitHub Actions matrix** (native builds per platform) | no cross-compilation; macOS uses `lipo` to produce a universal binary |
| First-release platforms | Windows / macOS (x86_64+arm64) / Linux (X11) | Wayland in phase two |

### Key Selection Rationale

- **C++ + Qt over Rust + Tauri**: the user chose to "link libav into a single binary". libav* is a C library; C++ can directly `#include` and link it, with no binding layer; Rust would need `rsmpeg`/`ffmpeg-next`, and static linking has friction with the cargo build system. Qt is the mature cross-platform GUI in the C++ ecosystem, with many precedents for combining with ffmpeg.
- **Pure C rejected**: libav integration would be more native, but C lacks a good cross-platform GUI, and the GUI half would cost too much.
- **Static linking + single binary**: the user clearly prefers single-binary distribution. The cost: enabling `x264` puts the whole binary under **GPL** (acceptable for open source; if a closed-source need arises later, switch to platform hardware encoders VideoToolbox / Media Foundation to avoid it).
- **MP4 first rather than GIF**: GIF needs two ffmpeg passes (palette + transcode), which is the worst offender for Peek's stop-hang; MP4 encodes fast and finalizes lightly.

## 3. High-Level Architecture

### Module Breakdown (each module has a single responsibility, a clear interface, and is independently testable)

```
+-------------------+        +------------------------+
|   Qt UI layer     |        |  RegionOverlay         |
|  (control bar /   |<------>|  translucent fullscreen|
|   settings)       |        |  window, drag a box    |
+-------------------+        |  produces CaptureRegion|
        |                    +------------------------+
        | start(region) / stop()
        v
+-------------------------------------------------+
|  Recorder (recording controller, worker thread) |
|  - abstract interface Recorder                  |
|  - implementation LibavRecorder                 |
|    open input(platform device) -> encode -> mux |
|    stop(): stop reading frames + av_write_trailer|
+-------------------------------------------------+
        |
        v
   platform input backend selection (gdigrab / avfoundation / x11grab)
```

### Key Interfaces (draft, refined in the implementation plan)

- `struct CaptureRegion { int x, y, w, h; int screenIndex; double dpiScale; }`
- `struct OutputOptions { std::string path; int fps; bool audioEnabled = false; /* system default input; specific device selection in phase two */ }`
- `class Recorder` (abstract base class): `start(CaptureRegion, OutputOptions)`, `stop()`, signals `finished(path)` / `error(msg)` / `progress(...)`.
  - Purpose of an abstract base class: v1 is `LibavRecorder`; reserve the option to later replace it with a platform hardware encoder or a CLI fallback implementation, without touching the UI layer.
- The platform backend is chosen via a factory by the current OS, selecting the libavdevice input format and device string.

### Data Flow

1. User clicks "Select Region" -> `RegionOverlay` drags a box fullscreen -> yields `CaptureRegion`.
2. User clicks "Record" -> UI calls `Recorder::start(region, options)`; recording runs on a worker thread (libavdevice grabs video frames -> libx264 encodes -> libavformat writes MP4). If `options.audioEnabled` (false by default), it simultaneously grabs frames from the system default audio input -> AAC encode -> multiplexed into the same MP4 (libswresample does any necessary resampling).
3. On macOS, because `avfoundation` cannot crop at the input, use `libavfilter`'s `crop=W:H:X:Y` to crop; X11/Windows carry the offset and dimensions directly in the input parameters.
4. User clicks "Stop" -> the worker thread stops reading frames, `av_write_trailer` finalizes, closes the file -> emits `finished(path)`. Throughout, the UI only shows "Finalizing..." and **does not block**.

### Error Handling

- Permissions: on macOS, the first run needs "screen recording" permission; on capture failure, guide the user to System Settings.
- Device/encode failure: reported via the `Recorder::error` signal, the UI shows a message, no crash.
- Stop-timeout fallback: if finalization abnormally times out, log it and release resources safely (still without synchronously waiting on the UI thread).

### Test Strategy

- `CaptureRegion` coordinate conversion (multi-monitor/DPI): pure functions, unit-tested.
- Platform backend parameter assembly: unit tests asserting the generated device/filter parameters.
- `Recorder` lifecycle: integration test that records a short clip and verifies the output MP4 decodes, the duration is correct, and stopping does not hang.

## 4. Build and Distribution

### Dependencies

- `vcpkg.json` declares `ffmpeg` (feature `x264`), etc.; static triplets: `x64-windows-static` / `x64-osx` / `arm64-osx` / `x64-linux`.
- **Qt must be statically built** (to meet the single-binary hard requirement): via the vcpkg `qtbase` static triplet (note the first Qt compile is slow, CI needs caching); in CMake use `Q_IMPORT_PLUGIN` to statically import platform plugins (qwindows / qcocoa / qxcb). `jurplel/install-qt-action` provides a dynamic Qt, which does not meet the requirement, used only as a fallback option.

### CI Matrix (GitHub Actions, all native builds, no cross-compilation)

| runner | target |
|---|---|
| `windows-latest` | Windows x64 |
| `macos-14` | macOS arm64 |
| `macos-13` | macOS x86_64 |
| `ubuntu-latest` | Linux x64 (X11) |

### Packaging

- Because Qt and libav are both statically linked, there is **no need for `windeployqt`/`macdeployqt` to collect dynamic libraries**; packaging just produces the final single file.
- Windows: directly produce a single `.exe` (optionally wrap an installer with Inno Setup/NSIS, but the exe itself already runs standalone).
- macOS: assemble a `.app` (single executable + Info.plist declaring the screen-recording purpose), `lipo` to merge arm64+x86_64 universal, sign/notarize, then `create-dmg` to produce a .dmg.
- Linux: package the static executable into a single AppImage with `linuxdeploy` + the appimage plugin.

### Release

- push a `v*` tag -> matrix builds -> per-platform deploy/packaging -> `actions/upload-artifact` staging -> `softprops/action-gh-release` attaches the `.exe/.dmg/.AppImage` to the corresponding GitHub Release.

## 5. Feature and Interaction Design

### 5.1 Recording Task List

The core of the main interface is a **recording list**, one row per recording, with a status label, file name, duration, and action buttons.

State machine:

```
Recording --stop--> Finalizing --trailer written--> Completed
Finalizing --process crash/force quit--> FinalizationInterrupted
any        --launch/encode error--> Failed
```

| State | Meaning |
|---|---|
| Recording | grabbing and encoding frames |
| Finalizing | stopped, writing the MP4 trailer |
| Completed | playable |
| FinalizationInterrupted | finalization not completed (crash/force quit) |
| Failed | launch/encode error |

- Per-row actions: **play / open containing folder / delete**.
- **Delete interaction**: a dialog with three choices -- `Remove from list only` / `Also delete the video file` / `Cancel`; default focus on "Cancel" to prevent accidental deletion.
- **Crash recovery**: recording uses **fragmented MP4** (`-movflags frag_keyframe+empty_moov`), so a file with interrupted finalization is still mostly playable. On app startup, scan the recording directory and metadata, flag "FinalizationInterrupted" entries, and offer "try to repair / rewrite the trailer".
- **List persistence**: recording entry metadata (path, status, duration, creation time) is stored in a local **JSON file** (entry counts are small, no need to introduce a DB dependency, fitting the single-binary goal), restored after restart.

### 5.2 Closing, Completion Notification, Playback

- **Closing the app with a "Finalizing" task in progress**: intercept the close event, **every time show a dialog with three choices** -- `Continue in background (minimize to tray)` / `Wait until done then exit` / `Force quit (may corrupt, relying on fragmented MP4 as a fallback)`.
- **Completion notification**: `QSystemTrayIcon` system notification "XXX has finished finalizing" + the list entry turns "Completed" + an optional sound cue.
- **Playback**: double-click an entry to invoke the system default player (`QDesktopServices::openUrl`); plus "open containing folder". v1 does not include a built-in player (YAGNI).

### 5.3 Recording Region

- Freely drag a rectangle (overlay window), showing the current `W x H` live.
- Preset sizes: `720p (1280 x 720)` / `1080p (1920 x 1080)` / `16:9` / `4:3` / `1:1`.
- Fullscreen recording.

### 5.4 Fullscreen Excluding Itself + Delayed Start

- **Hide before starting recording**: the main window, control bar, and region overlay are all hidden, ensuring the fullscreen image does not contain this app.
- **Delayed-start countdown**: optionally `0 / 3 / 5` seconds (default 3, 0 = immediate).
- During the countdown: a **large fullscreen number** prominently reminds the user, who switches to the content to be recorded; it also shows the hint "press `Ctrl+Alt+S` to stop".
- **Timing guarantee**: the countdown overlay closes/hides at the exact moment frame grabbing actually starts, ensuring the first recorded frame is already a clean image and the countdown is not recorded.

### 5.5 Stop and Global Hotkeys

- In fullscreen/hidden state, operate via **global hotkeys** (the main window is hidden, buttons cannot be clicked):
  - Stop = **`Ctrl+Alt+S`** (macOS: `Cmd+Option+S`).
  - Start/toggle = `Ctrl+Alt+R` (can act as a toggle).
  - Both customizable in settings.
- **Global hotkey implementation**: the QHotkey library or platform APIs -- Windows `RegisterHotKey`, macOS `RegisterEventHotKey` (no extra permission needed), Linux (X11) `XGrabKey`.
- **Conflict avoidance**: default values avoid `PrintScreen`, `Esc`, `Alt+F4`, `Ctrl+C/V/X/Z`, and `Win/Cmd` combinations; for user customization, do reserved-key/conflict validation and warn.
- The **tray menu** also provides "Stop/Start" entries, as a fallback when hotkeys fail.

## 6. Risks and Open Items

- **GPL constraint** (libx264 static linking): v1 treats it as open source; switch to platform hardware encoders when a closed-source need arises.
- **macOS permission UX**: need to handle the permission request and guidance well before the first recording.
- **Multi-monitor/DPI coordinates**: cross-platform behavioral differences, need focused testing.
- **Phase-two Wayland**: needs PipeWire + xdg-desktop-portal; the capture path is completely different from X11, designed separately.
- **Static Qt build cost**: compiling Qt statically with vcpkg takes a long time in CI, dependency caching is needed; this is the main engineering cost of the "single binary" requirement.
- A frontend framework (Svelte, etc.) is obsolete under the Qt approach -- the UI is all **Qt Widgets** (decided, no QML).
- Audio: v1 uses only the system default input, off by default; device selection/mixing is phase two.
- **Cross-platform global hotkey consistency**: the three platforms register differently and have different boundaries (under X11, `XGrabKey` grabs and may be affected by other grabbers), needs a unified wrapper and testing.
- **fragmented MP4 compatibility**: a few old players poorly support fragmented MP4, need to verify mainstream players; this is the cost of "playable even when finalization is interrupted".
- **Countdown not recorded**: the timing between the countdown overlay and the frame-grab start must be handled precisely, to avoid recording the hint in the first frame.
```

