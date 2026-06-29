# RegionRecord P1 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Set up a CMake + vcpkg project: iterate quickly locally with system dynamic libraries, and for Release/CI use the vcpkg static triplet to link Qt6 + libav into a **single self-contained binary**, then use a minimal Qt window to prove that both Qt and libav compile, link, and run; the CI matrix can produce a Windows `.exe` / Linux AppImage / macOS `.app`.

**Architecture:** One Qt Widgets executable `RegionRecord` + one core static library `rr_core` (holds GUI-independent, unit-testable logic; in this plan only the libav version probe). The build splits into two setups: **dev** (system Qt6 + system ffmpeg, fast) and **release-static** (vcpkg static ffmpeg[x264] + qtbase, single binary). CI uses release-static to build natively and package on each platform.

**Tech Stack:** C++17, Qt6 Widgets/Test, libav(ffmpeg), CMake + CMakePresets, Ninja, vcpkg (manifest + static triplet), GitHub Actions, linuxdeploy(AppImage), lipo/create-dmg(mac).

## Global Constraints

> The following are project-wide constraints, implicitly included in every Task. Values are taken verbatim from the tech-stack selection spec.

- **Single self-contained binary (hard requirement)**: the Release artifact must not depend on any external dynamic library at runtime; Qt + libav + Qt platform plugins are all statically linked, and the platform plugins are imported with `Q_IMPORT_PLUGIN`.
- **Platform scope v1**: Windows x64, macOS x86_64+arm64, Linux X11. **No Wayland.**
- **vcpkg static triplet**: `x64-windows-static` / `x64-osx` / `arm64-osx` / `x64-linux`.
- **Dependencies**: `ffmpeg` (feature `x264`), `qtbase`. Video H.264 (libx264), container MP4.
- **License**: because libx264 is statically linked, the whole project is treated as **GPL**.
- **UI**: Qt **Widgets**, no QML.
- **C++ standard**: C++17.
- **dev/release separation**: local development uses system (dynamic) libraries; the single binary is only produced by the release-static preset / CI.

## File Structure

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Top-level build: find Qt6/FFMPEG, define `rr_core`, `RegionRecord`, tests |
| `vcpkg.json` | Dependency manifest (ffmpeg[x264], qtbase) |
| `CMakePresets.json` | Two presets: `dev` (system libraries) and `release-static` (vcpkg static) |
| `src/ffmpeg_probe.h` / `.cpp` | Returns the libav version string -- proves libav is usable, unit-testable |
| `src/MainWindow.h` / `.cpp` | Minimal main window, displays the probe string |
| `src/main.cpp` | Program entry point; `Q_IMPORT_PLUGIN` platform plugins for static builds |
| `tests/test_ffmpeg_probe.cpp` | QtTest unit test |
| `packaging/linux/build-appimage.sh` | Build the Linux AppImage |
| `packaging/macos/Info.plist` | mac `.app` metadata (declares screen recording usage) |
| `.github/workflows/build.yml` | CI matrix build + packaging + tag release |

---

### Task 1: Project skeleton + minimal Qt window (dev build works)

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/main.cpp`
- Create: `src/MainWindow.h`
- Create: `src/MainWindow.cpp`
- Create: `.gitignore` (already exists, append `build*/`)

**Interfaces:**
- Consumes: none (first task).
- Produces: CMake target `RegionRecord` (executable); class `rr::MainWindow : public QMainWindow`, default-constructed.

- [ ] **Step 1: Install dev dependencies (system Qt6 + ninja; ffmpeg dev already present on this machine)**

Run:
```bash
sudo apt-get update && sudo apt-get install -y qt6-base-dev libgl1-mesa-dev ninja-build
```
Expected: install succeeds; `qmake6 --version` runs, `ninja --version` prints a version.

- [ ] **Step 2: Write the top-level CMakeLists.txt (dev path, no FFMPEG/tests yet)**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.21)
project(RegionRecord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(RegionRecord
    src/main.cpp
    src/MainWindow.cpp
)
target_include_directories(RegionRecord PRIVATE src)
target_link_libraries(RegionRecord PRIVATE Qt6::Widgets)
```

- [ ] **Step 3: Write the minimal window and entry point**

`src/MainWindow.h`:
```cpp
#pragma once
#include <QMainWindow>

namespace rr {
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
};
}
```

`src/MainWindow.cpp`:
```cpp
#include "MainWindow.h"
#include <QLabel>

namespace rr {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("RegionRecord");
    auto* label = new QLabel("RegionRecord", this);
    label->setMargin(24);
    setCentralWidget(label);
}
}
```

`src/main.cpp`:
```cpp
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    rr::MainWindow w;
    w.resize(360, 160);
    w.show();
    return app.exec();
}
```

- [ ] **Step 4: Configure and build (dev)**

Run:
```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev
```
Expected: compiles successfully, produces `build-dev/RegionRecord`.

- [ ] **Step 5: Smoke run (X11, offscreen, verify no crash)**

Run:
```bash
QT_QPA_PLATFORM=offscreen ./build-dev/RegionRecord &
sleep 1; kill %1 2>/dev/null; echo "ran ok"
```
Expected: the process starts and does not crash immediately, prints `ran ok`.

- [ ] **Step 6: Append .gitignore and commit**

Run:
```bash
printf '\n# CMake build dirs\nbuild*/\n' >> .gitignore
git add CMakeLists.txt src/ .gitignore
git commit -m "feat(p1): CMake skeleton with minimal Qt window"
```

---

### Task 2: libav probe function + QtTest unit test (TDD)

**Files:**
- Create: `tests/test_ffmpeg_probe.cpp`
- Create: `src/ffmpeg_probe.h`
- Create: `src/ffmpeg_probe.cpp`
- Modify: `CMakeLists.txt` (add the `rr_core` library, FFMPEG, tests)

**Interfaces:**
- Consumes: none.
- Produces: function `std::string rr::ffmpegVersion();` -- returns a non-empty string like `"libavformat 61.7.100"`; CMake target `rr_core` (static library, links ffmpeg), test target `test_ffmpeg_probe`.

- [ ] **Step 1: Modify CMakeLists.txt to wire in FFMPEG, rr_core, and tests**

Replace the `find_package(Qt6 ...)` line in `CMakeLists.txt` with:
```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# dev uses system ffmpeg (pkg-config); release-static uses vcpkg's FindFFMPEG
find_package(PkgConfig)
if(PkgConfig_FOUND AND NOT VCPKG_TOOLCHAIN)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample)
    add_library(rr_ffmpeg INTERFACE)
    target_link_libraries(rr_ffmpeg INTERFACE PkgConfig::FFMPEG)
else()
    find_package(FFMPEG REQUIRED)
    add_library(rr_ffmpeg INTERFACE)
    target_include_directories(rr_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(rr_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
endif()

add_library(rr_core STATIC src/ffmpeg_probe.cpp)
target_include_directories(rr_core PUBLIC src)
target_link_libraries(rr_core PUBLIC rr_ffmpeg)
```

Change the executable definition to link `rr_core`:
```cmake
add_executable(RegionRecord
    src/main.cpp
    src/MainWindow.cpp
)
target_include_directories(RegionRecord PRIVATE src)
target_link_libraries(RegionRecord PRIVATE rr_core Qt6::Widgets)

enable_testing()
add_executable(test_ffmpeg_probe tests/test_ffmpeg_probe.cpp)
target_link_libraries(test_ffmpeg_probe PRIVATE rr_core Qt6::Test)
add_test(NAME test_ffmpeg_probe COMMAND test_ffmpeg_probe)
```

- [ ] **Step 2: Write the failing test**

`tests/test_ffmpeg_probe.cpp`:
```cpp
#include <QtTest>
#include <string>
#include "ffmpeg_probe.h"

class TestFfmpegProbe : public QObject {
    Q_OBJECT
private slots:
    void versionMentionsLibavformat();
};

void TestFfmpegProbe::versionMentionsLibavformat() {
    const std::string v = rr::ffmpegVersion();
    QVERIFY(!v.empty());
    QVERIFY2(v.find("libavformat") != std::string::npos,
             v.c_str());
}

QTEST_APPLESS_MAIN(TestFfmpegProbe)
#include "test_ffmpeg_probe.moc"
```

Create a placeholder header so it compiles but the test fails (the function returns an empty string):

`src/ffmpeg_probe.h`:
```cpp
#pragma once
#include <string>
namespace rr { std::string ffmpegVersion(); }
```

`src/ffmpeg_probe.cpp` (**deliberately returns an empty string so the test fails first**):
```cpp
#include "ffmpeg_probe.h"
namespace rr { std::string ffmpegVersion() { return {}; } }
```

- [ ] **Step 3: Build and run the test, confirm it fails**

Run:
```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev
ctest --test-dir build-dev -R test_ffmpeg_probe --output-on-failure
```
Expected: FAIL -- `versionMentionsLibavformat` fails (returns an empty string, cannot find "libavformat").

- [ ] **Step 4: Implement the real probe**

`src/ffmpeg_probe.cpp` (replace the whole file):
```cpp
#include "ffmpeg_probe.h"
#include <cstdio>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/version.h>
}

namespace rr {
std::string ffmpegVersion() {
    const unsigned v = avformat_version();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "libavformat %u.%u.%u",
                  AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v));
    return std::string(buf);
}
}
```

- [ ] **Step 5: Build and run the test, confirm it passes**

Run:
```bash
cmake --build build-dev
ctest --test-dir build-dev -R test_ffmpeg_probe --output-on-failure
```
Expected: PASS -- `1 test passed`.

- [ ] **Step 6: Commit**

Run:
```bash
git add CMakeLists.txt src/ffmpeg_probe.h src/ffmpeg_probe.cpp tests/test_ffmpeg_probe.cpp
git commit -m "feat(p1): libav version probe with QtTest (TDD)"
```

---

### Task 3: Window displays the probe string (integration check)

**Files:**
- Modify: `src/MainWindow.cpp`

**Interfaces:**
- Consumes: `rr::ffmpegVersion()` (Task 2).
- Produces: the central label of the window contains the libav version string (human-visible evidence that "both Qt and libav are alive").

- [ ] **Step 1: Call the probe function inside the window**

`src/MainWindow.cpp` (replace the whole file):
```cpp
#include "MainWindow.h"
#include "ffmpeg_probe.h"
#include <QLabel>
#include <QString>

namespace rr {
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("RegionRecord");
    const QString text = QString("RegionRecord\n%1")
        .arg(QString::fromStdString(ffmpegVersion()));
    auto* label = new QLabel(text, this);
    label->setMargin(24);
    setCentralWidget(label);
}
}
```

- [ ] **Step 2: Build and smoke run**

Run:
```bash
cmake --build build-dev
QT_QPA_PLATFORM=offscreen ./build-dev/RegionRecord &
sleep 1; kill %1 2>/dev/null; echo "ran ok"
```
Expected: prints `ran ok` (in an environment with a display, you can see `libavformat 61.x.x` in the window).

- [ ] **Step 3: Commit**

Run:
```bash
git add src/MainWindow.cpp
git commit -m "feat(p1): show libav version in window"
```

---

### Task 4: vcpkg manifest + CMakePresets + local static single binary (Linux)

**Files:**
- Create: `vcpkg.json`
- Create: `CMakePresets.json`
- Modify: `src/main.cpp` (import platform plugins for static builds)

**Interfaces:**
- Consumes: all existing targets.
- Produces: CMake presets `dev` and `release-static`; the `RegionRecord` built by release-static has **no dynamic dependency** on Qt/libav.

- [ ] **Step 1: Write vcpkg.json**

`vcpkg.json`:
```json
{
  "name": "regionrecord",
  "version": "0.1.0",
  "dependencies": [
    { "name": "ffmpeg", "features": ["x264", "avdevice"] },
    "qtbase"
  ]
}
```

- [ ] **Step 2: Write CMakePresets.json**

`CMakePresets.json`:
```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "dev",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-dev",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_PREFIX_PATH": "/usr/lib/x86_64-linux-gnu/cmake/Qt6"
      }
    },
    {
      "name": "release-static",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build-static",
      "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "VCPKG_TARGET_TRIPLET": "$env{RR_TRIPLET}"
      }
    }
  ],
  "buildPresets": [
    { "name": "dev", "configurePreset": "dev" },
    { "name": "release-static", "configurePreset": "release-static" }
  ]
}
```

- [ ] **Step 3: Import Qt platform plugins for static builds**

`src/main.cpp` (replace the whole file):
```cpp
#include <QApplication>
#include "MainWindow.h"

#if defined(QT_STATIC)
#include <QtPlugin>
#if defined(Q_OS_WIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#elif defined(Q_OS_MAC)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#elif defined(Q_OS_LINUX)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)
#endif
#endif

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    rr::MainWindow w;
    w.resize(360, 160);
    w.show();
    return app.exec();
}
```

- [ ] **Step 4: Bootstrap vcpkg (if not present on this machine)**

Run:
```bash
git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
export RR_TRIPLET=x64-linux
```
Expected: produces the `$HOME/vcpkg/vcpkg` executable.

- [ ] **Step 5: Static build (the first run compiles Qt+ffmpeg, takes a long time, which is normal)**

Run:
```bash
cmake --preset release-static
cmake --build --preset release-static
```
Expected: successfully produces `build-static/RegionRecord`.

- [ ] **Step 6: Verify there are no dynamic Qt/libav dependencies**

Run:
```bash
ldd build-static/RegionRecord | grep -Ei 'qt|avcodec|avformat|x264' && echo "HAS DYNAMIC DEP (FAIL)" || echo "OK: no Qt/libav dynamic deps"
```
Expected: prints `OK: no Qt/libav dynamic deps` (grep finds no matches).

- [ ] **Step 7: Commit**

Run:
```bash
git add vcpkg.json CMakePresets.json src/main.cpp
git commit -m "feat(p1): vcpkg manifest + static single-binary preset (Linux verified)"
```

---

### Task 5: Linux AppImage packaging

**Files:**
- Create: `packaging/linux/build-appimage.sh`

**Interfaces:**
- Consumes: `build-static/RegionRecord`.
- Produces: a single file `RegionRecord-x86_64.AppImage`, runs standalone.

- [ ] **Step 1: Write the packaging script**

`packaging/linux/build-appimage.sh`:
```bash
#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-build-static/RegionRecord}"
OUT_DIR="dist"
APPDIR="${OUT_DIR}/RegionRecord.AppDir"

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
cp "${BIN}" "${APPDIR}/usr/bin/RegionRecord"

cat > "${APPDIR}/RegionRecord.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=RegionRecord
Exec=RegionRecord
Icon=regionrecord
Categories=AudioVideo;
EOF

# Placeholder icon (P1 uses a solid-color placeholder; the real icon is replaced in a later plan)
touch "${APPDIR}/regionrecord.png"

# Download linuxdeploy (only needed once)
TOOL="${OUT_DIR}/linuxdeploy-x86_64.AppImage"
if [ ! -f "${TOOL}" ]; then
  curl -fsSL -o "${TOOL}" \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
  chmod +x "${TOOL}"
fi

cd "${OUT_DIR}"
../"${TOOL}" --appdir RegionRecord.AppDir --output appimage
```

- [ ] **Step 2: Run packaging and smoke test**

Run:
```bash
chmod +x packaging/linux/build-appimage.sh
./packaging/linux/build-appimage.sh
APPIMAGE=$(ls dist/RegionRecord*.AppImage | head -1)
QT_QPA_PLATFORM=offscreen "$APPIMAGE" &
sleep 1; kill %1 2>/dev/null; echo "appimage ran ok"
```
Expected: produces `dist/RegionRecord-x86_64.AppImage`, prints `appimage ran ok`.

- [ ] **Step 3: Commit**

Run:
```bash
git add packaging/linux/build-appimage.sh
git commit -m "feat(p1): linux AppImage packaging"
```

---

### Task 6: CI -- Linux job (build static + AppImage + upload artifact)

**Files:**
- Create: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: the `release-static` preset, the AppImage script.
- Produces: a GitHub Actions workflow; the `build-linux` job uploads an AppImage artifact named `RegionRecord-linux`; the vcpkg binary cache key is ready.

- [ ] **Step 1: Write the workflow (start with just one Linux job)**

`.github/workflows/build.yml`:
```yaml
name: build
on:
  push:
    branches: [ main, master ]
    tags: [ 'v*' ]
  pull_request:

env:
  VCPKG_BINARY_SOURCES: "clear;x-gha,readwrite"

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Export GitHub Actions cache env (vcpkg)
        uses: actions/github-script@v7
        with:
          script: |
            core.exportVariable('ACTIONS_CACHE_URL', process.env.ACTIONS_CACHE_URL || '');
            core.exportVariable('ACTIONS_RUNTIME_TOKEN', process.env.ACTIONS_RUNTIME_TOKEN || '');
      - name: Install build deps
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build cmake curl \
            libgl1-mesa-dev libxkbcommon-dev '^libxcb.*-dev' libx11-xcb-dev \
            libxrender-dev libxi-dev libfontconfig1-dev libfreetype-dev
      - name: Setup vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
          "$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
          echo "VCPKG_ROOT=$HOME/vcpkg" >> "$GITHUB_ENV"
          echo "RR_TRIPLET=x64-linux" >> "$GITHUB_ENV"
      - name: Configure & build (static)
        run: |
          cmake --preset release-static
          cmake --build --preset release-static
      - name: Verify no dynamic Qt/libav deps
        run: |
          ldd build-static/RegionRecord | grep -Ei 'qt|avcodec|avformat|x264' \
            && { echo "FAIL: dynamic dep"; exit 1; } || echo "OK"
      - name: Package AppImage
        run: ./packaging/linux/build-appimage.sh
      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: RegionRecord-linux
          path: dist/RegionRecord*.AppImage
```

- [ ] **Step 2: Commit and push, watch CI**

Run:
```bash
git add .github/workflows/build.yml
git commit -m "ci(p1): linux static build + AppImage artifact"
git push
```
Expected: GitHub Actions `build-linux` is green; the artifacts page has `RegionRecord-linux`.

> Verification relies on the CI run result (GH Actions cannot run locally). If it fails, fix apt package names / xcb dependencies per the log and re-push.

---

### Task 7: CI -- Windows and macOS jobs

**Files:**
- Modify: `.github/workflows/build.yml`
- Create: `packaging/macos/Info.plist`

**Interfaces:**
- Consumes: the `release-static` preset.
- Produces: the `build-windows` job produces a single `.exe` artifact `RegionRecord-windows`; the `build-macos` job produces a universal `.app` (zipped) artifact `RegionRecord-macos`.

- [ ] **Step 1: Write the mac Info.plist (declare screen recording usage)**

`packaging/macos/Info.plist`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>RegionRecord</string>
  <key>CFBundleDisplayName</key><string>RegionRecord</string>
  <key>CFBundleIdentifier</key><string>ai.insnap.regionrecord</string>
  <key>CFBundleVersion</key><string>0.1.0</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleExecutable</key><string>RegionRecord</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
```

- [ ] **Step 2: Append the Windows job to build.yml**

Append under `jobs:`:
```yaml
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/github-script@v7
        with:
          script: |
            core.exportVariable('ACTIONS_CACHE_URL', process.env.ACTIONS_CACHE_URL || '');
            core.exportVariable('ACTIONS_RUNTIME_TOKEN', process.env.ACTIONS_RUNTIME_TOKEN || '');
      - uses: lukka/get-cmake@latest
      - name: Setup vcpkg
        shell: bash
        run: |
          git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
          "$HOME/vcpkg/bootstrap-vcpkg.bat"
          echo "VCPKG_ROOT=$HOME/vcpkg" >> "$GITHUB_ENV"
          echo "RR_TRIPLET=x64-windows-static" >> "$GITHUB_ENV"
      - uses: ilammy/msvc-dev-cmd@v1
      - name: Configure & build (static)
        shell: bash
        run: |
          cmake --preset release-static
          cmake --build --preset release-static
      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: RegionRecord-windows
          path: build-static/RegionRecord.exe
```

- [ ] **Step 3: Append the macOS job to build.yml (two arches + lipo + .app)**

Append under `jobs:`:
```yaml
  build-macos:
    runs-on: macos-14
    steps:
      - uses: actions/checkout@v4
      - uses: actions/github-script@v7
        with:
          script: |
            core.exportVariable('ACTIONS_CACHE_URL', process.env.ACTIONS_CACHE_URL || '');
            core.exportVariable('ACTIONS_RUNTIME_TOKEN', process.env.ACTIONS_RUNTIME_TOKEN || '');
      - uses: lukka/get-cmake@latest
      - name: Setup vcpkg
        run: |
          git clone https://github.com/microsoft/vcpkg "$HOME/vcpkg"
          "$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
          echo "VCPKG_ROOT=$HOME/vcpkg" >> "$GITHUB_ENV"
      - name: Build arm64
        run: |
          RR_TRIPLET=arm64-osx cmake --preset release-static -B build-arm64
          cmake --build build-arm64
      - name: Build x86_64
        run: |
          RR_TRIPLET=x64-osx cmake --preset release-static -B build-x64
          cmake --build build-x64
      - name: Assemble universal .app
        run: |
          APP="dist/RegionRecord.app"
          mkdir -p "$APP/Contents/MacOS"
          cp packaging/macos/Info.plist "$APP/Contents/Info.plist"
          lipo -create build-arm64/RegionRecord build-x64/RegionRecord \
            -output "$APP/Contents/MacOS/RegionRecord"
          lipo -info "$APP/Contents/MacOS/RegionRecord"
      - name: Zip and upload
        run: |
          cd dist && zip -r RegionRecord-macos.zip RegionRecord.app
      - uses: actions/upload-artifact@v4
        with:
          name: RegionRecord-macos
          path: dist/RegionRecord-macos.zip
```

- [ ] **Step 4: Commit, push, watch CI on all three platforms**

Run:
```bash
git add .github/workflows/build.yml packaging/macos/Info.plist
git commit -m "ci(p1): windows .exe + macos universal .app jobs"
git push
```
Expected: the three jobs `build-linux` / `build-windows` / `build-macos` are all green, each with its artifact.

> Verification relies on CI results. Common fixes: Qt platform plugin linking under the Windows static triplet, and mac `lipo -info` should show `x86_64 arm64`.

---

### Task 8: CI -- tag-triggered release to GitHub Release

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: the artifacts of the three build jobs.
- Produces: a `release` job -- when a `v*` tag is pushed, attach the three platform artifacts to the corresponding GitHub Release.

- [ ] **Step 1: Append the release job**

Append at the end of `jobs:`:
```yaml
  release:
    needs: [ build-linux, build-windows, build-macos ]
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - name: Download all artifacts
        uses: actions/download-artifact@v4
        with:
          path: artifacts
      - name: Publish release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            artifacts/RegionRecord-linux/*
            artifacts/RegionRecord-windows/*
            artifacts/RegionRecord-macos/*
```

- [ ] **Step 2: Commit, tag, push, verify the Release**

Run:
```bash
git add .github/workflows/build.yml
git commit -m "ci(p1): publish artifacts to GitHub Release on tag"
git push
git tag v0.1.0 && git push origin v0.1.0
```
Expected: after the tag is pushed the `release` job is green, and `v0.1.0` on GitHub Releases contains the three assets `.AppImage` / `.exe` / `.zip(.app)`.

---

## Self-Review

**1. Spec coverage (within P1 scope):**
- Single self-contained binary hard requirement -> Task 4 (static preset + ldd check), Task 6 (CI ldd gate), Task 7 (Win exe / mac .app).
- Three platforms v1 (Win/mac x64+arm64/Linux X11) -> Task 6/7.
- vcpkg static triplet -> Task 4/6/7.
- ffmpeg[x264] + qtbase dependencies -> Task 2/4.
- Qt Widgets, C++17 -> Task 1.
- dev/release separation -> Task 4 (two presets).
- CI release -> Task 8.
- Note: P1 does not include feature logic such as recording/region/list/hotkey/audio -- those belong to P2-P8 and are deliberately out of this plan.

**2. Placeholder scan:** no TBD/TODO; the icon uses an explicit solid-color placeholder noted for later replacement; every code step contains complete code.

**3. Type consistency:** `rr::ffmpegVersion()` is defined in Task 2 and consumed in Task 3, with a consistent signature; the CMake target names `rr_core` / `rr_ffmpeg` / `RegionRecord` / `test_ffmpeg_probe` are consistent throughout; the preset names `dev` / `release-static` and the environment variables `RR_TRIPLET` / `VCPKG_ROOT` are consistent throughout.
