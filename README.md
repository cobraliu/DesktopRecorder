# RegionRecord

A fast, cross-platform region screen recorder built with Qt 6 and FFmpeg, designed to be shipped as a **single self-contained executable** (one AppImage on Linux) with no external dynamic library dependencies.

It was built to replace tools that freeze when you stop recording: RegionRecord finalizes the MP4 reliably and can recover gracefully if finalization is interrupted.

## Features

- **Three capture modes**: fullscreen, drag-to-select region, and pick-a-window.
- **Live capture frame**: a translucent frame marks the recording area; click-through so you can keep working under it.
- **Always-available stop**: a floating "Stop" bar appears while recording, plus an optional global hotkey (`Ctrl+Alt+S`). The bar works even when the hotkey is grabbed by your desktop environment.
- **Delayed start**: optional 3s / 5s countdown before recording begins.
- **Configurable FPS** (default 10) and optional audio capture.
- **Recording history**: play, open the containing folder, or delete past recordings (with optional file deletion).
- **Dark, themed UI** with crisp vector icons (HiDPI-aware).

## Platform support

- **Linux / X11** — fully supported (capture via X11 + MIT-SHM, no external FFmpeg device).
- **Linux / Wayland** — capture via `xdg-desktop-portal` ScreenCast + PipeWire (see below).
- **Windows / macOS** — capture, hotkey, window-pick, and audio backends are implemented;
  runtime verification on real hardware is ongoing. macOS builds are **Apple Silicon
  (arm64) only**; Intel (x86_64) Macs are not supported.

### Wayland notes

On a native Wayland session, X11 grabbing only sees XWayland and reads back black, so
the backend is selected at runtime: when `WAYLAND_DISPLAY` is set (or
`XDG_SESSION_TYPE=wayland`), capture goes through `xdg-desktop-portal`'s ScreenCast
interface and a PipeWire video stream; otherwise the X11 + MIT-SHM path is used.

- The compositor shows **its own consent picker** when recording starts — you choose which
  output to share. The drag-selected rectangle is then **cropped client-side** within that
  output (the portal does not grant arbitrary screen rectangles).
- The click-through capture frame (`setMask` hole) and the global stop hotkey (`XGrabKey`)
  are X11-only mechanisms; on Wayland the floating **Stop HUD** remains the reliable stop path.
- **Audio** uses the same ALSA input device as X11; on PipeWire desktops ALSA is routed
  through `pipewire-alsa`, so no separate Wayland audio backend is needed.
- The PipeWire **client** library is linked into the single binary; it connects to the
  session's PipeWire daemon at runtime, just as the X11 backend connects to the X server.

## Building

Requirements: CMake ≥ 3.21, a C++17 compiler, Qt 6 (incl. Qt DBus), FFMPEG (libav*), and —
for the Wayland capture backend — the PipeWire client headers (`libpipewire-0.3-dev`).

### Development build (system Qt + FFmpeg)

```bash
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug
cmake --build build-dev -j
ctest --test-dir build-dev
```

### Release build (static, single binary via vcpkg)

```bash
cmake --preset release-static   # uses vcpkg static triplet
cmake --build build-static -j
```

The resulting `build-static/RegionRecord` links libav*, x264 and Qt statically — verify with `ldd`.

### Packaging (Linux AppImage)

```bash
packaging/linux/build-appimage.sh
```

## License

Released under the [MIT License](LICENSE).
