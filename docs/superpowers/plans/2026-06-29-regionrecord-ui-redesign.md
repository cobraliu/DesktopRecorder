# RegionRecord UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps are tracked with checkboxes.

**Goal:** Consolidate region selection into three modes (fullscreen / drag-select / pick-window), where drag-select and pick-window use a Peek-style translucent floating frame (the transparent hole is not captured; during recording a persistent red frame indicates the recording area), and give the main window a modern look with a dark teal accent.

**Architecture:** (1) `frame_geometry` pure functions do the "floating outer frame <-> recording hole" conversion (TDD). (2) `CaptureFrameWindow`, a borderless translucent floating frame: a truly transparent hole in the middle (CompositionMode_Clear), with a border around it (teal and draggable/resizable in edit state, red with mouse pass-through in recording state). (3) `WindowPickerX11` uses XGrabPointer to click-pick a window and get its current geometry. (4) `theme` provides a dark QSS. (5) `MainWindow` rewrite: segmented three-mode selection + card-style history list + a prominent record button + empty state.

**Tech Stack:** Qt6 Widgets + QSS, Xlib (XGrabPointer), libav* (unchanged).

## Global Constraints

- The single static binary is unchanged; do not introduce new third-party libraries (QSS is built in, Xlib is already linked).
- The recording region semantics are unchanged: `CaptureRegion{x,y,w,h,...}`, dimensions rounded to even.
- The transparent hole depends on a compositor; without a compositor it degrades to translucent (still functional, the hole may not be fully transparent -> noted as a known limitation).
- Remove the use of resolution presets in the UI; `fullscreenRegion()` is retained.

---

### Task 1: frame_geometry conversion (TDD)

**Files:** Create `src/ui/frame_geometry.h/.cpp`, `tests/app/test_frame_geometry.cpp`; Modify `CMakeLists.txt` (add rr_ui sources + test).

**Interfaces (Produces):**
- `CaptureRegion holeRegionFromFrame(const QRect& frameGeom, int border);` hole = outer frame inset by border on all four sides, width/height rounded to even.
- `QRect frameGeomForRegion(const CaptureRegion& r, int border);` outer frame = hole expanded by border on all four sides.

- [ ] **Step 1: Failing test** `test_frame_geometry.cpp`:
```cpp
#include <QtTest>
#include "ui/frame_geometry.h"
class TestFrameGeometry : public QObject { Q_OBJECT
private slots:
    void holeInsetsByBorder();
    void roundTrip();
};
void TestFrameGeometry::holeInsetsByBorder() {
    const rr::CaptureRegion h = rr::holeRegionFromFrame(QRect(100,100,408,308), 4);
    QCOMPARE(h.x, 104); QCOMPARE(h.y, 104);
    QCOMPARE(h.w, 400); QCOMPARE(h.h, 300);
}
void TestFrameGeometry::roundTrip() {
    rr::CaptureRegion r; r.x=200; r.y=150; r.w=640; r.h=480;
    const QRect f = rr::frameGeomForRegion(r, 6);
    QCOMPARE(f, QRect(194,144,652,492));
    const rr::CaptureRegion h = rr::holeRegionFromFrame(f, 6);
    QCOMPARE(h.x, r.x); QCOMPARE(h.y, r.y);
    QCOMPARE(h.w, r.w); QCOMPARE(h.h, r.h);
}
QTEST_MAIN(TestFrameGeometry)
#include "test_frame_geometry.moc"
```
- [ ] **Step 2: Implement** `frame_geometry.cpp`: evenDown the width/height; clamp min to 2.
- [ ] **Step 3: CMake** add `src/ui/frame_geometry.cpp` to rr_ui; register `test_frame_geometry` (link rr_ui Qt6::Test).
- [ ] **Step 4: Build + tests pass.**
- [ ] **Step 5: commit** `feat(ui): frame<->hole geometry helpers (TDD)`

---

### Task 2: CaptureFrameWindow -- Peek-style floating frame

**Files:** Create `src/ui/CaptureFrameWindow.h/.cpp`; Modify `CMakeLists.txt` (rr_ui sources).

**Interfaces (Produces):**
- `void beginEditing(const CaptureRegion& initial);` show edit state (teal border, draggable/resizable).
- `void enterRecordingStyle();` switch to red + `setAttribute(Qt::WA_TransparentForMouseEvents,true)`, keep showing.
- `void setRegionGeometry(const CaptureRegion&);` position the outer frame by region.
- `CaptureRegion captureRegion() const;` = `holeRegionFromFrame(geometry(), border_)`.

Key points:
- Construction: `Qt::FramelessWindowHint|WindowStaysOnTopHint|Tool`, `WA_TranslucentBackground`, `border_=4`, `minHole 80x60`.
- `paintEvent`: fill the whole window with the border color (edit #19c3a3 / recording #e5484d); punch the hole transparent with `CompositionMode_Clear`; draw `width x height` text in the top-left of the hole.
- Edit-state `mousePress/Move/Release`: hit-test move/resize within an 8px edge band (record globalPosition and original geometry at press; move translates, resize adjusts per the hit edge, clamping to minimum size); cursor changes with the region (SizeFDiag, etc.).
- Recording state: mouse pass-through, no event handling.

- [ ] **Step 1** Header (the interfaces above + members origin_/origStart_/mode_, etc.).
- [ ] **Step 2** Implement paint/move/resize/style switching.
- [ ] **Step 3** CMake: add sources to rr_ui; build.
- [ ] **Step 4** Smoke: under offscreen, after `beginEditing`, `captureRegion()` equals the initial region (can be added to test_frame_geometry or a separate smoke; create the GUI with QT_QPA_PLATFORM=offscreen).
- [ ] **Step 5** commit `feat(ui): Peek-style translucent CaptureFrameWindow`

---

### Task 3: WindowPickerX11 -- click-pick a window for its geometry

**Files:** Create `src/platform/WindowPickerX11.h/.cpp`; Modify `CMakeLists.txt` (rr_app sources).

**Interfaces (Produces):**
- `class WindowPickerX11 { public: bool pickBlocking(CaptureRegion& out); };`
  - Owns its own `XOpenDisplay`; `XGrabPointer` (crosshair cursor XC_crosshair); `XNextEvent` waits for ButtonPress (take `xbutton.subwindow`, or root if None) or KeyPress (Esc -> return false).
  - Target window geometry: `XGetGeometry` for width/height, `XTranslateCoordinates(dpy, win, root, 0,0,&ax,&ay,&child)` for the absolute origin; clamp width/height to the screen, round to even.
  - `XUngrabPointer` + `XCloseDisplay`.

- [ ] **Step 1** Header + implementation (refer to the standalone Display usage in `GlobalHotkeyX11.cpp`).
- [ ] **Step 2** CMake: add sources to rr_app (X11 already linked); build.
- [ ] **Step 3** Manual verification (integration, needs DISPLAY + mouse): clicking a window yields a reasonable rectangle. No automated test (interactive), record as SKIP.
- [ ] **Step 4** commit `feat(platform): X11 click-to-pick window region`

---

### Task 4: Dark teal theme QSS

**Files:** Create `src/ui/theme.h/.cpp`; Modify `src/main.cpp`, `CMakeLists.txt`.

**Interfaces (Produces):** `QString darkTealStyleSheet();` (for `qApp->setStyleSheet`).

Key points: `main.cpp` sets `QApplication::setStyle("Fusion")` + dark palette + `app.setStyleSheet(rr::darkTealStyleSheet())`. QSS overrides: window background #1e2227, text #e6e6e6, accent #19c3a3; QPushButton rounded corners/hover; `#recordBtn` large teal; segmented buttons (checkable, accent fill when selected); QComboBox/QSpinBox/QCheckBox rounded with outline; cards `#card` rounded #262b31 + hover highlight; badge classes `.badge-*` colors.

- [ ] **Step 1** theme.cpp: write the QSS string.
- [ ] **Step 2** main.cpp: apply Fusion + palette + QSS.
- [ ] **Step 3** CMake: add theme.cpp to rr_ui; build; RegionRecord starts under offscreen without crashing.
- [ ] **Step 4** commit `feat(ui): dark teal theme (Fusion + QSS)`

---

### Task 5: MainWindow rewrite (three modes + card list + recording indicator)

**Files:** Modify `src/ui/MainWindow.h/.cpp`. (Remove the use of RegionSelectorOverlay; the file does not have to be deleted.)

Key points:
- Top "control card": segmented three-mode `QButtonGroup` (fullscreen / drag-select / pick-window, exclusive, default drag-select), delay combo, FPS spin, audio check; below it a large `#recordBtn` "(dot) Start Recording".
- Mode behavior:
  - Select **drag-select**: `captureFrame_->beginEditing(default centered 960x540)`.
  - Select **fullscreen**: hide captureFrame_.
  - Select **pick-window**: `WindowPickerX11.pickBlocking` -> on success `captureFrame_->beginEditing(picked)` (fine-tunable); on cancel revert to the previous mode.
- `onStartClicked`: fullscreen -> `fullscreenRegion`; otherwise -> `captureFrame_->captureRegion()`. `hide()` the main window; for some modes `captureFrame_->enterRecordingStyle()` keeps the red frame persistent; countdown -> `beginCapture` starts recording with that region.
- `onCompleted`/stop: `captureFrame_->hide()` (or return to edit state), main window `showNormal`.
- History list: `QListWidget` + `setItemWidget` cards (emoji + elided file name + colored status badge + duration / relative time + play/delete buttons). When there are no entries, show a centered empty-state `QLabel` (the list and the empty state are shown one or the other).
- Status badge colors: Recording = red, Finalizing = amber, Completed = green, FinalizationInterrupted = orange, Failed = gray-red.

- [ ] **Step 1** Edit MainWindow.h (members: three-mode buttons, `CaptureFrameWindow* captureFrame_`, empty-state label; remove selector_/regionBox_).
- [ ] **Step 2** Rewrite MainWindow.cpp layout + modes + recording flow + card list + empty state.
- [ ] **Step 3** Build; under `QT_QPA_PLATFORM=offscreen` run test_mainwindow (construct + show) and pass.
- [ ] **Step 4** On a real machine `DISPLAY=:1` walk through manually: drag-select dragging, pick-window clicking, recording red frame, stop, card list, play/delete.
- [ ] **Step 5** Full `ctest` passes.
- [ ] **Step 6** commit `feat(ui): three region modes + card history list + recording frame`

---

## Self-Review

**Coverage:** three modes (fullscreen / drag-select / pick-window) + recording-area indicator (red frame not captured) + preset removal + dark teal look, each mapping to the user's three rounds of feedback and the chosen visual direction.
**Consistency:** `holeRegionFromFrame`/`frameGeomForRegion`/`captureRegion()`/`beginEditing`/`enterRecordingStyle`/`pickBlocking`/`darkTealStyleSheet` are consistent across Tasks.
**Known limitations:** the transparent hole depends on a compositor; pick-window takes the "current" geometry and does not follow window moves; fullscreen recording has no out-of-frame indicator (fullscreen is self-evident anyway); WindowPicker is interactive with no automated test.
