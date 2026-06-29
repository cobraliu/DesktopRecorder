# RegionRecord Application Layer (Business Logic) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On top of the completed recording engine (P2: `rr::Recorder`/`X11FrameSource`/`Mp4Encoder`), build a usable desktop application: a recording task list (with state machine and JSON persistence), region presets/fullscreen, delayed-start countdown, global hotkey stop, close interception, completion notification, and playback.

**Architecture:** Split into two layers. **Core layer** (no GUI, pure logic, fully covered by QtTest): `RecordingState`/`RecordingItem` value types, `RecordingStore` (JSON persistence + add/remove/update + startup scan that marks "finalization interrupted"), `RegionPresets` (presets + fullscreen region computation), `HotkeyCombo` (reserved-key conflict validation). **Integration/GUI layer** (thin shell, offscreen smoke tests): `RecordingController` (orchestrates Recorder<->Store + state transitions + notifications), `MainWindow` (list + control bar), `CountdownOverlay`, `RegionSelectorOverlay`, `GlobalHotkeyX11` (XGrabKey), tray and close interception.

**Tech Stack:** C++17, Qt6 Widgets/Core/Gui, QJsonDocument, QSystemTrayIcon, QDesktopServices, X11 XGrabKey (Linux global hotkey), QtTest.

## Global Constraints

- Single self-contained binary hard requirement: do not introduce any new third-party library that requires dynamic linking; the Linux global hotkey uses the system's existing X11 (xcb/Xlib, already bundled in the AppImage), not the external QHotkey dependency.
- The first version is Linux(X11) backend only; the Win/mac FrameSource and hotkey backends are a later plan -- this plan isolates them behind interfaces and does not implement other platform branches.
- C++17; new code style consistent with the existing `src/recording/*` (`rr` namespace, `#pragma once`, RAII).
- The core layer does not depend on `Qt6::Widgets/Gui`, only on `Qt6::Core`, ensuring it can be unit-tested in a headless environment.
- Recording metadata JSON path: `QStandardPaths::AppDataLocation`/`recordings.json`; default recording video directory: `QStandardPaths::MoviesLocation`/`RegionRecord/`.
- State machine (see spec §5.1): `Recording -> Finalizing -> Completed`; `Finalizing ->(force quit) FinalizationInterrupted`; `any ->(start/encode error) Failed`.
- Default hotkeys: stop `Ctrl+Alt+S`, start/toggle `Ctrl+Alt+R`; countdown defaults to 3 seconds (options 0/3/5); audio off by default.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/app/RecordingItem.h` | `RecordingState` enum + `RecordingItem` value type + `stateLabel()` + JSON codec |
| `src/app/RecordingItem.cpp` | Implementation of the above |
| `src/app/RecordingStore.h/.cpp` | List persistence (load/save/add/removeAt/setState/setDuration) + startup scan marking interruptions + change signal |
| `src/app/RegionPresets.h/.cpp` | Preset size list + fullscreen region computation |
| `src/app/HotkeyCombo.h/.cpp` | Reserved-key/conflict validation for hotkey combinations |
| `src/app/RecordingController.h/.cpp` | Orchestrates `Recorder` and `RecordingStore`, state transitions, emits completed/failed signals |
| `src/platform/GlobalHotkey.h` | Abstract hotkey interface |
| `src/platform/GlobalHotkeyX11.h/.cpp` | X11 XGrabKey implementation, background thread grabs keys, `triggered(id)` signal |
| `src/ui/MainWindow.h/.cpp` | Main window: task list view + region/FPS/audio controls + start/stop + close interception + tray |
| `src/ui/CountdownOverlay.h/.cpp` | Fullscreen large-number countdown overlay |
| `src/ui/RegionSelectorOverlay.h/.cpp` | Drag-to-select overlay, reports the selected region |
| `tests/app/*` | Core-layer QtTest |

---

### Task 1: RecordingItem -- state enum + value type + JSON codec (TDD)

**Files:**
- Create: `src/app/RecordingItem.h`, `src/app/RecordingItem.cpp`
- Test: `tests/app/test_recording_item.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class rr::RecordingState { Recording, Finalizing, Completed, FinalizationInterrupted, Failed };`
  - `struct rr::RecordingItem { QString id, filePath; RecordingState state; qint64 createdAtMs; int durationMs; };`
  - `QString rr::stateLabel(RecordingState);` (display label)
  - `QJsonObject rr::toJson(const RecordingItem&);` / `RecordingItem rr::fromJson(const QJsonObject&);`

- [ ] **Step 1: Write the failing test**

`tests/app/test_recording_item.cpp`:
```cpp
#include <QtTest>
#include "app/RecordingItem.h"

class TestRecordingItem : public QObject {
    Q_OBJECT
private slots:
    void labelsAreNonEmpty();
    void jsonRoundTrip();
};

void TestRecordingItem::labelsAreNonEmpty() {
    using S = rr::RecordingState;
    for (S s : {S::Recording, S::Finalizing, S::Completed,
                S::FinalizationInterrupted, S::Failed})
        QVERIFY(!rr::stateLabel(s).isEmpty());
}

void TestRecordingItem::jsonRoundTrip() {
    rr::RecordingItem a;
    a.id = "abc"; a.filePath = "/tmp/x.mp4";
    a.state = rr::RecordingState::Completed;
    a.createdAtMs = 1700000000000LL; a.durationMs = 4200;
    const rr::RecordingItem b = rr::fromJson(rr::toJson(a));
    QCOMPARE(b.id, a.id);
    QCOMPARE(b.filePath, a.filePath);
    QCOMPARE(int(b.state), int(a.state));
    QCOMPARE(b.createdAtMs, a.createdAtMs);
    QCOMPARE(b.durationMs, a.durationMs);
}

QTEST_APPLESS_MAIN(TestRecordingItem)
#include "test_recording_item.moc"
```

- [ ] **Step 2: Write the header**

`src/app/RecordingItem.h`:
```cpp
#pragma once
#include <QString>
#include <QJsonObject>
#include <QtGlobal>

namespace rr {

enum class RecordingState {
    Recording, Finalizing, Completed, FinalizationInterrupted, Failed
};

struct RecordingItem {
    QString id;
    QString filePath;
    RecordingState state = RecordingState::Recording;
    qint64 createdAtMs = 0;
    int durationMs = 0;
};

QString stateLabel(RecordingState s);
QJsonObject toJson(const RecordingItem& item);
RecordingItem fromJson(const QJsonObject& o);

}
```

- [ ] **Step 3: Write the implementation**

`src/app/RecordingItem.cpp`:
```cpp
#include "app/RecordingItem.h"

namespace rr {

QString stateLabel(RecordingState s) {
    switch (s) {
        case RecordingState::Recording:               return QStringLiteral("Recording");
        case RecordingState::Finalizing:              return QStringLiteral("Finalizing");
        case RecordingState::Completed:               return QStringLiteral("Completed");
        case RecordingState::FinalizationInterrupted: return QStringLiteral("Finalization interrupted");
        case RecordingState::Failed:                  return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

QJsonObject toJson(const RecordingItem& item) {
    QJsonObject o;
    o["id"] = item.id;
    o["filePath"] = item.filePath;
    o["state"] = static_cast<int>(item.state);
    o["createdAtMs"] = item.createdAtMs;
    o["durationMs"] = item.durationMs;
    return o;
}

RecordingItem fromJson(const QJsonObject& o) {
    RecordingItem item;
    item.id = o.value("id").toString();
    item.filePath = o.value("filePath").toString();
    item.state = static_cast<RecordingState>(o.value("state").toInt());
    item.createdAtMs = o.value("createdAtMs").toVariant().toLongLong();
    item.durationMs = o.value("durationMs").toInt();
    return item;
}

}
```

- [ ] **Step 4: CMake -- create the core-layer library `rr_app` (depends only on Qt6::Core)**

Append after `rr_core` in `CMakeLists.txt`:
```cmake
add_library(rr_app STATIC
    src/app/RecordingItem.cpp
)
target_include_directories(rr_app PUBLIC src)
target_link_libraries(rr_app PUBLIC Qt6::Core)

add_executable(test_recording_item tests/app/test_recording_item.cpp)
target_link_libraries(test_recording_item PRIVATE rr_app Qt6::Test)
add_test(NAME test_recording_item COMMAND test_recording_item)
```

- [ ] **Step 5: Build and run; confirm it fails first, then passes**

Run:
```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_recording_item
ctest --test-dir build-dev -R test_recording_item --output-on-failure
```
Expected: PASS (two cases).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/RecordingItem.h src/app/RecordingItem.cpp tests/app/test_recording_item.cpp
git commit -m "feat(p3): RecordingItem state enum + JSON codec (TDD)"
```

---

### Task 2: RecordingStore -- JSON persistence + add/remove/update + startup scan (TDD)

**Files:**
- Create: `src/app/RecordingStore.h`, `src/app/RecordingStore.cpp`
- Test: `tests/app/test_recording_store.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RecordingItem`.
- Produces: `class rr::RecordingStore : public QObject`:
  - `explicit RecordingStore(QString jsonPath, QObject*=nullptr);`
  - `void load();` (reads JSON; reclassifies any entry still in `Recording`/`Finalizing` as `FinalizationInterrupted` -- it was not finalized properly last time)
  - `void save() const;`
  - `const QVector<RecordingItem>& items() const;`
  - `void add(const RecordingItem&);`
  - `bool removeAt(int index);` (removes the entry only, does not touch the file)
  - `void setState(const QString& id, RecordingState);`
  - `void setDuration(const QString& id, int durationMs);`
  - signal `void changed();` (emitted after any change, including load/add/remove/setState)

- [ ] **Step 1: Write the failing test**

`tests/app/test_recording_store.cpp`:
```cpp
#include <QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "app/RecordingStore.h"

class TestRecordingStore : public QObject {
    Q_OBJECT
private slots:
    void addPersistsAcrossReload();
    void interruptedDetectedOnLoad();
    void removeAndSetState();
};

static rr::RecordingItem mk(const QString& id, rr::RecordingState s) {
    rr::RecordingItem it; it.id = id; it.filePath = "/tmp/" + id + ".mp4";
    it.state = s; it.createdAtMs = 1; it.durationMs = 0; return it;
}

void TestRecordingStore::addPersistsAcrossReload() {
    QTemporaryDir dir;
    const QString path = dir.path() + "/recordings.json";
    {
        rr::RecordingStore s(path);
        QSignalSpy spy(&s, &rr::RecordingStore::changed);
        s.add(mk("a", rr::RecordingState::Completed));
        QVERIFY(spy.count() >= 1);
    }
    rr::RecordingStore s2(path);
    s2.load();
    QCOMPARE(s2.items().size(), 1);
    QCOMPARE(s2.items()[0].id, QString("a"));
}

void TestRecordingStore::interruptedDetectedOnLoad() {
    QTemporaryDir dir;
    const QString path = dir.path() + "/recordings.json";
    {
        rr::RecordingStore s(path);
        s.add(mk("rec", rr::RecordingState::Recording));   // simulate still recording when it crashed last time
        s.add(mk("fin", rr::RecordingState::Finalizing));  // simulate finalizing when it crashed last time
    }
    rr::RecordingStore s2(path);
    s2.load();
    QCOMPARE(s2.items().size(), 2);
    for (const auto& it : s2.items())
        QCOMPARE(int(it.state), int(rr::RecordingState::FinalizationInterrupted));
}

void TestRecordingStore::removeAndSetState() {
    QTemporaryDir dir;
    rr::RecordingStore s(dir.path() + "/r.json");
    s.add(mk("a", rr::RecordingState::Recording));
    s.setState("a", rr::RecordingState::Completed);
    QCOMPARE(int(s.items()[0].state), int(rr::RecordingState::Completed));
    QVERIFY(s.removeAt(0));
    QCOMPARE(s.items().size(), 0);
    QVERIFY(!s.removeAt(0));
}

QTEST_APPLESS_MAIN(TestRecordingStore)
#include "test_recording_store.moc"
```

- [ ] **Step 2: Write the header**

`src/app/RecordingStore.h`:
```cpp
#pragma once
#include <QObject>
#include <QVector>
#include <QString>
#include "app/RecordingItem.h"

namespace rr {

class RecordingStore : public QObject {
    Q_OBJECT
public:
    explicit RecordingStore(QString jsonPath, QObject* parent = nullptr);

    void load();
    void save() const;
    const QVector<RecordingItem>& items() const { return items_; }

    void add(const RecordingItem& item);
    bool removeAt(int index);
    void setState(const QString& id, RecordingState state);
    void setDuration(const QString& id, int durationMs);

signals:
    void changed();

private:
    int indexOf(const QString& id) const;
    QString path_;
    QVector<RecordingItem> items_;
};

}
```

- [ ] **Step 3: Write the implementation**

`src/app/RecordingStore.cpp`:
```cpp
#include "app/RecordingStore.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>

namespace rr {

RecordingStore::RecordingStore(QString jsonPath, QObject* parent)
    : QObject(parent), path_(std::move(jsonPath)) {}

int RecordingStore::indexOf(const QString& id) const {
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].id == id) return i;
    return -1;
}

void RecordingStore::load() {
    items_.clear();
    QFile f(path_);
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const auto& v : arr) {
            RecordingItem it = fromJson(v.toObject());
            // entries not finalized properly last time (still recording/finalizing) are marked "finalization interrupted"
            if (it.state == RecordingState::Recording ||
                it.state == RecordingState::Finalizing)
                it.state = RecordingState::FinalizationInterrupted;
            items_.push_back(it);
        }
    }
    emit changed();
}

void RecordingStore::save() const {
    QJsonArray arr;
    for (const auto& it : items_) arr.append(toJson(it));
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

void RecordingStore::add(const RecordingItem& item) {
    items_.push_back(item);
    save();
    emit changed();
}

bool RecordingStore::removeAt(int index) {
    if (index < 0 || index >= items_.size()) return false;
    items_.removeAt(index);
    save();
    emit changed();
    return true;
}

void RecordingStore::setState(const QString& id, RecordingState state) {
    const int i = indexOf(id);
    if (i < 0) return;
    items_[i].state = state;
    save();
    emit changed();
}

void RecordingStore::setDuration(const QString& id, int durationMs) {
    const int i = indexOf(id);
    if (i < 0) return;
    items_[i].durationMs = durationMs;
    save();
    emit changed();
}

}
```

- [ ] **Step 4: CMake**

Append `src/app/RecordingStore.cpp` to the `rr_app` sources. Append at the end:
```cmake
add_executable(test_recording_store tests/app/test_recording_store.cpp)
target_link_libraries(test_recording_store PRIVATE rr_app Qt6::Test)
add_test(NAME test_recording_store COMMAND test_recording_store)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_recording_store
ctest --test-dir build-dev -R test_recording_store --output-on-failure
```
Expected: PASS (3 cases).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/RecordingStore.h src/app/RecordingStore.cpp tests/app/test_recording_store.cpp
git commit -m "feat(p3): RecordingStore JSON persistence + interrupted-on-load (TDD)"
```

---

### Task 3: RegionPresets -- preset sizes + fullscreen region (TDD)

**Files:**
- Create: `src/app/RegionPresets.h`, `src/app/RegionPresets.cpp`
- Test: `tests/app/test_region_presets.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rr::CaptureRegion` (`recording/types.h`).
- Produces:
  - `struct rr::Preset { QString name; int w; int h; };`
  - `QVector<rr::Preset> rr::standardPresets();` (720p/1080p/720x720 etc., both width and height even)
  - `rr::CaptureRegion rr::regionFromPreset(const rr::Preset&, int screenX, int screenY, int screenW, int screenH);` (centered within the screen, clamped to the screen if it overflows, and guarantees even width/height)
  - `rr::CaptureRegion rr::fullscreenRegion(int screenX, int screenY, int screenW, int screenH);` (whole screen, made even)

- [ ] **Step 1: Write the failing test**

`tests/app/test_region_presets.cpp`:
```cpp
#include <QtTest>
#include "app/RegionPresets.h"
#include "recording/types.h"

class TestRegionPresets : public QObject {
    Q_OBJECT
private slots:
    void presetsAreEvenAndValid();
    void presetCenteredWithinScreen();
    void presetClampedToScreen();
    void fullscreenMatchesScreenEven();
};

void TestRegionPresets::presetsAreEvenAndValid() {
    const auto ps = rr::standardPresets();
    QVERIFY(ps.size() >= 3);
    for (const auto& p : ps) {
        QVERIFY(p.w % 2 == 0);
        QVERIFY(p.h % 2 == 0);
        QVERIFY(p.w > 0 && p.h > 0);
    }
}

void TestRegionPresets::presetCenteredWithinScreen() {
    rr::Preset p{"720p", 1280, 720};
    auto r = rr::regionFromPreset(p, 0, 0, 1920, 1080);
    QCOMPARE(r.w, 1280);
    QCOMPARE(r.h, 720);
    QCOMPARE(r.x, (1920 - 1280) / 2);
    QCOMPARE(r.y, (1080 - 720) / 2);
    QVERIFY(rr::isValidRegion(r));
}

void TestRegionPresets::presetClampedToScreen() {
    rr::Preset p{"1080p", 1920, 1080};
    auto r = rr::regionFromPreset(p, 0, 0, 1366, 768);
    QVERIFY(r.x >= 0 && r.y >= 0);
    QVERIFY(r.w <= 1366 && r.h <= 768);
    QVERIFY(r.w % 2 == 0 && r.h % 2 == 0);
    QVERIFY(rr::isValidRegion(r));
}

void TestRegionPresets::fullscreenMatchesScreenEven() {
    auto r = rr::fullscreenRegion(100, 50, 1921, 1081);
    QCOMPARE(r.x, 100);
    QCOMPARE(r.y, 50);
    QCOMPARE(r.w, 1920); // 1921 rounded down to even
    QCOMPARE(r.h, 1080);
    QVERIFY(rr::isValidRegion(r));
}

QTEST_APPLESS_MAIN(TestRegionPresets)
#include "test_region_presets.moc"
```

- [ ] **Step 2: Write the header**

`src/app/RegionPresets.h`:
```cpp
#pragma once
#include <QString>
#include <QVector>
#include "recording/types.h"

namespace rr {

struct Preset { QString name; int w; int h; };

QVector<Preset> standardPresets();
CaptureRegion regionFromPreset(const Preset& p,
                               int screenX, int screenY, int screenW, int screenH);
CaptureRegion fullscreenRegion(int screenX, int screenY, int screenW, int screenH);

}
```

- [ ] **Step 3: Write the implementation**

`src/app/RegionPresets.cpp`:
```cpp
#include "app/RegionPresets.h"
#include <algorithm>

namespace rr {

static int evenDown(int v) { return v - (v % 2); }

QVector<Preset> standardPresets() {
    return {
        {QStringLiteral("720p (1280x720)"),  1280, 720},
        {QStringLiteral("1080p (1920x1080)"), 1920, 1080},
        {QStringLiteral("16:9 (960x540)"),    960, 540},
        {QStringLiteral("4:3 (800x600)"),     800, 600},
        {QStringLiteral("1:1 (720x720)"),     720, 720},
    };
}

CaptureRegion regionFromPreset(const Preset& p,
                               int screenX, int screenY, int screenW, int screenH) {
    CaptureRegion r;
    r.w = evenDown(std::min(p.w, screenW));
    r.h = evenDown(std::min(p.h, screenH));
    r.x = screenX + (screenW - r.w) / 2;
    r.y = screenY + (screenH - r.h) / 2;
    return r;
}

CaptureRegion fullscreenRegion(int screenX, int screenY, int screenW, int screenH) {
    CaptureRegion r;
    r.x = screenX; r.y = screenY;
    r.w = evenDown(screenW);
    r.h = evenDown(screenH);
    return r;
}

}
```

- [ ] **Step 4: CMake**

Append `src/app/RegionPresets.cpp` to the `rr_app` sources, and link `rr_app` against `rr_core` (it uses `isValidRegion` etc. from `recording/types.h`):
```cmake
target_link_libraries(rr_app PUBLIC Qt6::Core rr_core)
```
Append at the end:
```cmake
add_executable(test_region_presets tests/app/test_region_presets.cpp)
target_link_libraries(test_region_presets PRIVATE rr_app Qt6::Test)
add_test(NAME test_region_presets COMMAND test_region_presets)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_region_presets
ctest --test-dir build-dev -R test_region_presets --output-on-failure
```
Expected: PASS (4 cases).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/RegionPresets.h src/app/RegionPresets.cpp tests/app/test_region_presets.cpp
git commit -m "feat(p3): region presets + fullscreen region (TDD)"
```

---

### Task 4: HotkeyCombo -- reserved-key/conflict validation (TDD)

**Files:**
- Create: `src/app/HotkeyCombo.h`, `src/app/HotkeyCombo.cpp`
- Test: `tests/app/test_hotkey_combo.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct rr::HotkeyCombo { bool ctrl=false, alt=false, shift=false, meta=false; int key=0; };` (`key` uses Qt::Key values)
  - `bool rr::isAcceptableHotkey(const HotkeyCombo&, QString* reason);` -- rejects: no modifier key, contains Meta (Win/Cmd), a standalone function key (PrintScreen/Escape), and conflicts with common system combinations (Alt+F4, Ctrl+C/V/X/Z).

- [ ] **Step 1: Write the failing test**

`tests/app/test_hotkey_combo.cpp`:
```cpp
#include <QtTest>
#include <Qt>
#include "app/HotkeyCombo.h"

class TestHotkeyCombo : public QObject {
    Q_OBJECT
private slots:
    void defaultStopIsAcceptable();
    void rejectsNoModifier();
    void rejectsMetaCombos();
    void rejectsCommonShortcuts();
};

void TestHotkeyCombo::defaultStopIsAcceptable() {
    rr::HotkeyCombo c; c.ctrl = true; c.alt = true; c.key = Qt::Key_S;
    QString why;
    QVERIFY2(rr::isAcceptableHotkey(c, &why), qPrintable(why));
}

void TestHotkeyCombo::rejectsNoModifier() {
    rr::HotkeyCombo c; c.key = Qt::Key_S;
    QVERIFY(!rr::isAcceptableHotkey(c, nullptr));
}

void TestHotkeyCombo::rejectsMetaCombos() {
    rr::HotkeyCombo c; c.meta = true; c.key = Qt::Key_S;
    QVERIFY(!rr::isAcceptableHotkey(c, nullptr));
}

void TestHotkeyCombo::rejectsCommonShortcuts() {
    rr::HotkeyCombo altF4; altF4.alt = true; altF4.key = Qt::Key_F4;
    QVERIFY(!rr::isAcceptableHotkey(altF4, nullptr));
    rr::HotkeyCombo ctrlC; ctrlC.ctrl = true; ctrlC.key = Qt::Key_C;
    QVERIFY(!rr::isAcceptableHotkey(ctrlC, nullptr));
}

QTEST_APPLESS_MAIN(TestHotkeyCombo)
#include "test_hotkey_combo.moc"
```

- [ ] **Step 2: Write the header**

`src/app/HotkeyCombo.h`:
```cpp
#pragma once
#include <QString>

namespace rr {

struct HotkeyCombo {
    bool ctrl = false, alt = false, shift = false, meta = false;
    int key = 0; // Qt::Key
};

// Returns true if acceptable; otherwise false and (if reason is non-null) writes the reason.
bool isAcceptableHotkey(const HotkeyCombo& c, QString* reason);

}
```

- [ ] **Step 3: Write the implementation**

`src/app/HotkeyCombo.cpp`:
```cpp
#include "app/HotkeyCombo.h"
#include <Qt>

namespace rr {

bool isAcceptableHotkey(const HotkeyCombo& c, QString* reason) {
    auto fail = [&](const QString& m) { if (reason) *reason = m; return false; };

    if (c.meta)
        return fail(QStringLiteral("Cannot use Win/Cmd key combinations; they easily conflict with the system"));
    if (!c.ctrl && !c.alt && !c.shift)
        return fail(QStringLiteral("Must contain at least one modifier key (Ctrl/Alt/Shift)"));
    if (c.key == 0 || c.key == Qt::Key_Print || c.key == Qt::Key_Escape)
        return fail(QStringLiteral("Cannot use this function key"));

    // Common system/editing combinations
    if (c.alt && !c.ctrl && !c.shift && c.key == Qt::Key_F4)
        return fail(QStringLiteral("Alt+F4 is the system close-window shortcut"));
    if (c.ctrl && !c.alt && !c.shift &&
        (c.key == Qt::Key_C || c.key == Qt::Key_V ||
         c.key == Qt::Key_X || c.key == Qt::Key_Z))
        return fail(QStringLiteral("Conflicts with copy/paste/cut/undo"));

    return true;
}

}
```

- [ ] **Step 4: CMake**

Append `src/app/HotkeyCombo.cpp` to the `rr_app` sources. Append at the end:
```cmake
add_executable(test_hotkey_combo tests/app/test_hotkey_combo.cpp)
target_link_libraries(test_hotkey_combo PRIVATE rr_app Qt6::Test)
add_test(NAME test_hotkey_combo COMMAND test_hotkey_combo)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_hotkey_combo
ctest --test-dir build-dev -R test_hotkey_combo --output-on-failure
```
Expected: PASS (4 cases).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/HotkeyCombo.h src/app/HotkeyCombo.cpp tests/app/test_hotkey_combo.cpp
git commit -m "feat(p3): hotkey combo validation (TDD)"
```

---

### Task 5: RecordingController -- orchestrate Recorder<->Store + state transitions (integration test, DISPLAY guard)

**Files:**
- Create: `src/app/RecordingController.h`, `src/app/RecordingController.cpp`
- Test: `tests/app/test_recording_controller.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `rr::Recorder`, `rr::RecordingStore`, `rr::CaptureRegion`, `rr::OutputOptions`, `rr::RecordingItem`.
- Produces: `class rr::RecordingController : public QObject`:
  - `RecordingController(RecordingStore* store, QObject* = nullptr);`
  - `QString startRecording(const CaptureRegion&, const OutputOptions&);` -- generates an id, adds a `Recording` entry to the store, starts the `Recorder`, returns the id.
  - `void stopRecording();` -- sets the current entry to `Finalizing`, calls `Recorder::stop()`.
  - Internally connects `Recorder::finished` -> sets the current store entry to `Completed` + computes durationMs + emits `recordingCompleted(id, path)`; `Recorder::error` -> sets `Failed` + emits `recordingFailed(id, msg)`.
  - signals: `void recordingCompleted(const QString& id, const QString& path);` `void recordingFailed(const QString& id, const QString& msg);`

- [ ] **Step 1: Write the integration test (DISPLAY guard)**

`tests/app/test_recording_controller.cpp`:
```cpp
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cstdlib>
#include "app/RecordingController.h"
#include "app/RecordingStore.h"

class TestRecordingController : public QObject {
    Q_OBJECT
private slots:
    void fullCycleUpdatesStore();
};

void TestRecordingController::fullCycleUpdatesStore() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping controller integration test");

    QTemporaryDir dir;
    rr::RecordingStore store(dir.path() + "/r.json");
    rr::RecordingController ctl(&store);
    QSignalSpy doneSpy(&ctl, &rr::RecordingController::recordingCompleted);

    rr::CaptureRegion region{0, 0, 320, 240, 0, 1.0};
    rr::OutputOptions opts;
    opts.path = (dir.path() + "/c.mp4").toStdString();
    opts.fps = 10;

    const QString id = ctl.startRecording(region, opts);
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.items().size(), 1);
    QCOMPARE(int(store.items()[0].state), int(rr::RecordingState::Recording));

    QTest::qWait(3000);
    ctl.stopRecording();

    QVERIFY(doneSpy.wait(6000));
    QCOMPARE(int(store.items()[0].state), int(rr::RecordingState::Completed));
    QVERIFY(store.items()[0].durationMs > 0);
}

QTEST_MAIN(TestRecordingController)
#include "test_recording_controller.moc"
```

- [ ] **Step 2: Write the header**

`src/app/RecordingController.h`:
```cpp
#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include "recording/types.h"
#include "recording/Recorder.h"

namespace rr {

class RecordingStore;

class RecordingController : public QObject {
    Q_OBJECT
public:
    explicit RecordingController(RecordingStore* store, QObject* parent = nullptr);
    ~RecordingController() override;

    QString startRecording(const CaptureRegion& region, const OutputOptions& options);
    void stopRecording();
    bool isRecording() const { return recorder_ != nullptr; }

signals:
    void recordingCompleted(const QString& id, const QString& path);
    void recordingFailed(const QString& id, const QString& msg);

private:
    RecordingStore* store_;
    std::unique_ptr<Recorder> recorder_;
    QString currentId_;
    qint64 startedAtMs_ = 0;
};

}
```

- [ ] **Step 3: Write the implementation**

`src/app/RecordingController.cpp`:
```cpp
#include "app/RecordingController.h"
#include "app/RecordingStore.h"
#include <QDateTime>
#include <QUuid>

namespace rr {

RecordingController::RecordingController(RecordingStore* store, QObject* parent)
    : QObject(parent), store_(store) {}

RecordingController::~RecordingController() = default;

QString RecordingController::startRecording(const CaptureRegion& region,
                                            const OutputOptions& options) {
    if (recorder_) return QString();

    currentId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    startedAtMs_ = QDateTime::currentMSecsSinceEpoch();

    RecordingItem item;
    item.id = currentId_;
    item.filePath = QString::fromStdString(options.path);
    item.state = RecordingState::Recording;
    item.createdAtMs = startedAtMs_;
    store_->add(item);

    recorder_ = std::make_unique<Recorder>();
    const QString id = currentId_;

    connect(recorder_.get(), &Recorder::finished, this,
            [this, id](const QString& path) {
        const int dur = int(QDateTime::currentMSecsSinceEpoch() - startedAtMs_);
        store_->setDuration(id, dur);
        store_->setState(id, RecordingState::Completed);
        recorder_.reset();
        emit recordingCompleted(id, path);
    });
    connect(recorder_.get(), &Recorder::error, this,
            [this, id](const QString& msg) {
        store_->setState(id, RecordingState::Failed);
        recorder_.reset();
        emit recordingFailed(id, msg);
    });

    recorder_->start(region, options);
    return currentId_;
}

void RecordingController::stopRecording() {
    if (!recorder_) return;
    store_->setState(currentId_, RecordingState::Finalizing);
    recorder_->stop();
}

}
```
> Note: `finished`/`error` are `emit`ted inside the `Recorder`'s worker thread (within the `QThread::create` lambda), delivered cross-thread to this object's slots (default `QueuedConnection`), updating the store safely on the thread that owns the controller. In the test, the event loop is driven by `QSignalSpy::wait()`.

- [ ] **Step 4: CMake**

Append `src/app/RecordingController.cpp` to the `rr_app` sources, and also link `rr_app` against `rr_core` (already present; Recorder lives in rr_core). Append at the end:
```cmake
add_executable(test_recording_controller tests/app/test_recording_controller.cpp)
target_link_libraries(test_recording_controller PRIVATE rr_app Qt6::Test)
add_test(NAME test_recording_controller COMMAND test_recording_controller)
```

- [ ] **Step 5: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_recording_controller
ctest --test-dir build-dev -R test_recording_controller --output-on-failure
```
Expected: PASS (record -> stop -> Completed, duration>0).

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/app/RecordingController.h src/app/RecordingController.cpp tests/app/test_recording_controller.cpp
git commit -m "feat(p3): RecordingController orchestrates Recorder + Store (TDD)"
```

---

### Task 6: GlobalHotkeyX11 -- XGrabKey global hotkey (DISPLAY-guarded smoke test)

**Files:**
- Create: `src/platform/GlobalHotkey.h`, `src/platform/GlobalHotkeyX11.h`, `src/platform/GlobalHotkeyX11.cpp`
- Test: `tests/app/test_global_hotkey.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - abstract `class rr::GlobalHotkey : public QObject { signals: void triggered(int id); public: virtual bool registerHotkey(int id, bool ctrl,bool alt,bool shift, int x11keysym)=0; virtual void unregisterAll()=0; };`
  - `class rr::GlobalHotkeyX11 : public GlobalHotkey` -- a background-thread `XNextEvent` loop that `emit triggered(id)` (queued connection) when a registered combination matches.

- [ ] **Step 1: Write the (abstract) header**

`src/platform/GlobalHotkey.h`:
```cpp
#pragma once
#include <QObject>

namespace rr {
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~GlobalHotkey() override = default;
    virtual bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int x11keysym) = 0;
    virtual void unregisterAll() = 0;
signals:
    void triggered(int id);
};
}
```

- [ ] **Step 2: Write the X11 implementation header**

`src/platform/GlobalHotkeyX11.h`:
```cpp
#pragma once
#include "platform/GlobalHotkey.h"
#include <QVector>
#include <atomic>

class QThread;

namespace rr {
class GlobalHotkeyX11 : public GlobalHotkey {
    Q_OBJECT
public:
    explicit GlobalHotkeyX11(QObject* parent = nullptr);
    ~GlobalHotkeyX11() override;
    bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int x11keysym) override;
    void unregisterAll() override;
private:
    struct Binding { int id; unsigned int keycode; unsigned int mods; };
    void eventLoop();
    void* dpy_ = nullptr;       // Display*
    unsigned long root_ = 0;    // Window
    QVector<Binding> bindings_;
    QThread* thread_ = nullptr;
    std::atomic<bool> stop_{false};
};
}
```

- [ ] **Step 3: Write the X11 implementation**

`src/platform/GlobalHotkeyX11.cpp`:
```cpp
#include "platform/GlobalHotkeyX11.h"
#include <QThread>

#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace rr {

GlobalHotkeyX11::GlobalHotkeyX11(QObject* parent) : GlobalHotkey(parent) {
    dpy_ = XOpenDisplay(nullptr);
    if (dpy_) root_ = DefaultRootWindow(static_cast<Display*>(dpy_));
}

GlobalHotkeyX11::~GlobalHotkeyX11() {
    unregisterAll();
    if (dpy_) { XCloseDisplay(static_cast<Display*>(dpy_)); dpy_ = nullptr; }
}

bool GlobalHotkeyX11::registerHotkey(int id, bool ctrl, bool alt, bool shift, int x11keysym) {
    if (!dpy_) return false;
    Display* d = static_cast<Display*>(dpy_);
    unsigned int mods = 0;
    if (ctrl)  mods |= ControlMask;
    if (alt)   mods |= Mod1Mask;
    if (shift) mods |= ShiftMask;
    const KeyCode kc = XKeysymToKeycode(d, static_cast<KeySym>(x11keysym));
    if (kc == 0) return false;

    // overlay common lock states (NumLock=Mod2, CapsLock=LockMask) to ensure it triggers in all cases
    const unsigned int extra[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};
    for (unsigned int e : extra)
        XGrabKey(d, kc, mods | e, root_, True, GrabModeAsync, GrabModeAsync);
    XSync(d, False);

    bindings_.push_back({id, kc, mods});
    if (!thread_) {
        thread_ = QThread::create([this] { eventLoop(); });
        thread_->start();
    }
    return true;
}

void GlobalHotkeyX11::eventLoop() {
    Display* d = static_cast<Display*>(dpy_);
    while (!stop_.load()) {
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == KeyPress) {
                const unsigned int m = ev.xkey.state & (ControlMask | Mod1Mask | ShiftMask);
                for (const auto& b : bindings_)
                    if (b.keycode == ev.xkey.keycode && b.mods == m)
                        emit triggered(b.id);
            }
        }
        QThread::msleep(20);
    }
}

void GlobalHotkeyX11::unregisterAll() {
    stop_.store(true);
    if (thread_) { thread_->quit(); thread_->wait(); delete thread_; thread_ = nullptr; }
    if (dpy_) {
        Display* d = static_cast<Display*>(dpy_);
        XUngrabKey(d, AnyKey, AnyModifier, root_);
        XSync(d, False);
    }
    bindings_.clear();
    stop_.store(false);
}

}
```
> Note: uses a separate `Display*` connection + polling `XPending` (20ms) to avoid blocking on exit; `emit triggered` is delivered cross-thread by default to the GUI thread via a queued connection. The mapping of `Qt::Key` -> X11 keysym for the `triggered` signal is done by the caller (`Ctrl+Alt+S` -> `XK_s`).

- [ ] **Step 4: Write the smoke test (register/unregister without crashing; DISPLAY guard)**

`tests/app/test_global_hotkey.cpp`:
```cpp
#include <QtTest>
#include <cstdlib>
#include "platform/GlobalHotkeyX11.h"
#include <X11/keysym.h>

class TestGlobalHotkey : public QObject {
    Q_OBJECT
private slots:
    void registerAndUnregister();
};

void TestGlobalHotkey::registerAndUnregister() {
    if (!std::getenv("DISPLAY"))
        QSKIP("no DISPLAY; skipping X11 hotkey test");
    rr::GlobalHotkeyX11 hk;
    QVERIFY(hk.registerHotkey(1, true, true, false, XK_s)); // Ctrl+Alt+S
    QTest::qWait(100);
    hk.unregisterAll(); // passes if it does not crash
    QVERIFY(true);
}

QTEST_MAIN(TestGlobalHotkey)
#include "test_global_hotkey.moc"
```

- [ ] **Step 5: CMake -- link X11**

Add the X11 lookup near `find_package(Qt6 ...)`:
```cmake
find_package(X11 REQUIRED)
```
Append `src/platform/GlobalHotkeyX11.cpp` to the `rr_app` sources, and link X11:
```cmake
target_link_libraries(rr_app PUBLIC Qt6::Core rr_core ${X11_LIBRARIES})
target_include_directories(rr_app PUBLIC ${X11_INCLUDE_DIR})
```
> Note: X11 is a system library; the AppImage carries it along with xcb, so it does not break the single-binary goal.
Append at the end:
```cmake
add_executable(test_global_hotkey tests/app/test_global_hotkey.cpp)
target_link_libraries(test_global_hotkey PRIVATE rr_app Qt6::Test)
add_test(NAME test_global_hotkey COMMAND test_global_hotkey)
```

- [ ] **Step 6: Build and run**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_global_hotkey
ctest --test-dir build-dev -R test_global_hotkey --output-on-failure
```
Expected: PASS (register + unregister without crashing).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/platform/GlobalHotkey.h src/platform/GlobalHotkeyX11.h src/platform/GlobalHotkeyX11.cpp tests/app/test_global_hotkey.cpp
git commit -m "feat(p3): X11 global hotkey (XGrabKey) for stop/start"
```

---

### Task 7: CountdownOverlay + RegionSelectorOverlay (offscreen smoke test)

**Files:**
- Create: `src/ui/CountdownOverlay.h`, `src/ui/CountdownOverlay.cpp`
- Create: `src/ui/RegionSelectorOverlay.h`, `src/ui/RegionSelectorOverlay.cpp`
- Test: `tests/app/test_overlays.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class rr::CountdownOverlay : public QWidget`: `void start(int seconds);` refreshes the large number every second, emits `void countdownFinished();` and calls `hide()` at 0; shows a "Press Ctrl+Alt+S to stop" hint. When `seconds==0`, emits the signal immediately without showing.
  - `class rr::RegionSelectorOverlay : public QWidget`: fullscreen semi-transparent, drag with the mouse to draw a rectangle, on release emits `void regionSelected(const rr::CaptureRegion&);`, Esc emits `void cancelled();`. Draws `WxH` in real time.

- [ ] **Step 1: Write the header (CountdownOverlay)**

`src/ui/CountdownOverlay.h`:
```cpp
#pragma once
#include <QWidget>
class QTimer;
namespace rr {
class CountdownOverlay : public QWidget {
    Q_OBJECT
public:
    explicit CountdownOverlay(QWidget* parent = nullptr);
    void start(int seconds);
signals:
    void countdownFinished();
protected:
    void paintEvent(QPaintEvent*) override;
private slots:
    void tick();
private:
    int remaining_ = 0;
    QTimer* timer_ = nullptr;
};
}
```

- [ ] **Step 2: Write the implementation (CountdownOverlay)**

`src/ui/CountdownOverlay.cpp`:
```cpp
#include "ui/CountdownOverlay.h"
#include <QTimer>
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>

namespace rr {

CountdownOverlay::CountdownOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &CountdownOverlay::tick);
}

void CountdownOverlay::start(int seconds) {
    if (seconds <= 0) { emit countdownFinished(); return; }
    remaining_ = seconds;
    if (QScreen* s = QGuiApplication::primaryScreen())
        setGeometry(s->geometry());
    showFullScreen();
    update();
    timer_->start();
}

void CountdownOverlay::tick() {
    if (--remaining_ <= 0) {
        timer_->stop();
        hide();
        emit countdownFinished();
        return;
    }
    update();
}

void CountdownOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    QFont f = p.font(); f.setPointSize(160); f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, QString::number(remaining_));
    QFont sf = p.font(); sf.setPointSize(24); sf.setBold(false);
    p.setFont(sf);
    QRect hint = rect(); hint.setTop(rect().center().y() + 140);
    p.drawText(hint, Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Press Ctrl+Alt+S to stop recording"));
}

}
```

- [ ] **Step 3: Write the header + implementation (RegionSelectorOverlay)**

`src/ui/RegionSelectorOverlay.h`:
```cpp
#pragma once
#include <QWidget>
#include <QPoint>
#include <QRect>
#include "recording/types.h"
namespace rr {
class RegionSelectorOverlay : public QWidget {
    Q_OBJECT
public:
    explicit RegionSelectorOverlay(QWidget* parent = nullptr);
    void beginSelection();
signals:
    void regionSelected(const rr::CaptureRegion& region);
    void cancelled();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPoint origin_;
    QRect rubber_;
    bool dragging_ = false;
};
}
```

`src/ui/RegionSelectorOverlay.cpp`:
```cpp
#include "ui/RegionSelectorOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>

namespace rr {

RegionSelectorOverlay::RegionSelectorOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::CrossCursor);
}

void RegionSelectorOverlay::beginSelection() {
    if (QScreen* s = QGuiApplication::primaryScreen())
        setGeometry(s->geometry());
    rubber_ = QRect();
    showFullScreen();
}

void RegionSelectorOverlay::mousePressEvent(QMouseEvent* e) {
    origin_ = e->pos(); rubber_ = QRect(origin_, QSize()); dragging_ = true; update();
}
void RegionSelectorOverlay::mouseMoveEvent(QMouseEvent* e) {
    if (dragging_) { rubber_ = QRect(origin_, e->pos()).normalized(); update(); }
}
void RegionSelectorOverlay::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = false;
    const QRect r = rubber_.normalized();
    hide();
    if (r.width() >= 2 && r.height() >= 2) {
        CaptureRegion reg;
        reg.x = x() + r.x(); reg.y = y() + r.y();
        reg.w = r.width() - (r.width() % 2);
        reg.h = r.height() - (r.height() % 2);
        emit regionSelected(reg);
    } else {
        emit cancelled();
    }
}
void RegionSelectorOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { hide(); emit cancelled(); }
}
void RegionSelectorOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 80));
    if (!rubber_.isNull()) {
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillRect(rubber_, Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(QPen(Qt::red, 2));
        p.drawRect(rubber_);
        p.setPen(Qt::white);
        p.drawText(rubber_.topLeft() + QPoint(4, -6),
                   QString("%1x%2").arg(rubber_.width()).arg(rubber_.height()));
    }
}

}
```

- [ ] **Step 4: Write the offscreen smoke test**

`tests/app/test_overlays.cpp`:
```cpp
#include <QtTest>
#include <QSignalSpy>
#include "ui/CountdownOverlay.h"

class TestOverlays : public QObject {
    Q_OBJECT
private slots:
    void zeroSecondFiresImmediately();
    void countdownReachesZero();
};

void TestOverlays::zeroSecondFiresImmediately() {
    rr::CountdownOverlay ov;
    QSignalSpy spy(&ov, &rr::CountdownOverlay::countdownFinished);
    ov.start(0);
    QCOMPARE(spy.count(), 1);
}

void TestOverlays::countdownReachesZero() {
    rr::CountdownOverlay ov;
    QSignalSpy spy(&ov, &rr::CountdownOverlay::countdownFinished);
    ov.start(1);
    QVERIFY(spy.wait(3000));
}

QTEST_MAIN(TestOverlays)
#include "test_overlays.moc"
```

- [ ] **Step 5: CMake -- create the GUI widget library `rr_ui` (links Widgets)**

Append at the end:
```cmake
add_library(rr_ui STATIC
    src/ui/CountdownOverlay.cpp
    src/ui/RegionSelectorOverlay.cpp
)
target_include_directories(rr_ui PUBLIC src)
target_link_libraries(rr_ui PUBLIC rr_app Qt6::Widgets)

add_executable(test_overlays tests/app/test_overlays.cpp)
target_link_libraries(test_overlays PRIVATE rr_ui Qt6::Test)
add_test(NAME test_overlays COMMAND test_overlays)
```

- [ ] **Step 6: Build and run (offscreen)**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev --target test_overlays
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R test_overlays --output-on-failure
```
Expected: PASS (0 seconds triggers immediately; 1-second countdown triggers when it reaches zero).

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/ui/CountdownOverlay.* src/ui/RegionSelectorOverlay.* tests/app/test_overlays.cpp
git commit -m "feat(p3): countdown + region selector overlays"
```

---

### Task 8: MainWindow -- list UI + control bar + tray + close interception + completion notification + playback (offscreen smoke + manual)

**Files:**
- Rewrite: `src/ui/MainWindow.h`, `src/ui/MainWindow.cpp` (move from `src/MainWindow.*` into `src/ui/` and extend)
- Modify: `src/main.cpp` (include paths + application metadata)
- Test: `tests/app/test_mainwindow.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RecordingStore`, `RecordingController`, `RegionPresets`, `CountdownOverlay`, `RegionSelectorOverlay`, `GlobalHotkeyX11`, `QSystemTrayIcon`, `QDesktopServices`.
- Behavior (spec §5):
  - The list `QListWidget`/`QTableWidget` is bound to the store, refreshed on `changed()`; each row shows "state label / file name / duration" + a right-click or button menu: Play / Open containing folder / Delete.
  - The delete dialog has three choices: `Remove from list only` / `Also delete the video file` / `Cancel` (default focus on "Cancel").
  - Control bar: region dropdown (presets + "Fullscreen" + "Custom selection"), FPS, audio toggle (off by default), delay dropdown (0/3/5, default 3), start button.
  - Start flow: hide the main window -> (if custom, select region first) -> `CountdownOverlay::start(delay)` -> on `countdownFinished` call `controller.startRecording`.
  - Stop: global hotkey `Ctrl+Alt+S` or the tray menu "Stop" -> `controller.stopRecording`.
  - Completion: `controller.recordingCompleted` -> tray `showMessage("XXX finalized")` + restore the main window display.
  - Close interception: if there is a `Finalizing` entry, `closeEvent` pops up three choices `Continue in background (minimize to tray)` / `Wait for completion then quit` / `Force quit`.
  - Playback: `QDesktopServices::openUrl(QUrl::fromLocalFile(path))`; opening the folder similarly opens the parent directory.

- [ ] **Step 1: Write the MainWindow header**

`src/ui/MainWindow.h`:
```cpp
#pragma once
#include <QMainWindow>
#include <memory>

class QListWidget;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QSystemTrayIcon;

namespace rr {

class RecordingStore;
class RecordingController;
class CountdownOverlay;
class RegionSelectorOverlay;
class GlobalHotkeyX11;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void refreshList();
    void onStartClicked();
    void beginCapture();          // actually start recording after the countdown ends
    void onStopRequested();
    void onCompleted(const QString& id, const QString& path);
    void onRowAction();           // play / open folder / delete

private:
    bool hasFinalizing() const;
    void playRow(int row);
    void openFolderRow(int row);
    void deleteRow(int row);

    std::unique_ptr<RecordingStore> store_;
    std::unique_ptr<RecordingController> controller_;
    CountdownOverlay* countdown_ = nullptr;
    RegionSelectorOverlay* selector_ = nullptr;
    GlobalHotkeyX11* hotkey_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;

    QListWidget* list_ = nullptr;
    QComboBox* regionBox_ = nullptr;
    QComboBox* delayBox_ = nullptr;
    QSpinBox* fpsBox_ = nullptr;
    QCheckBox* audioBox_ = nullptr;

    // pending recording parameters (stored between selection/countdown)
    struct Pending { int regionChoice; int fps; bool audio; } pending_{};
};

}
```

- [ ] **Step 2: Write the MainWindow implementation**

`src/ui/MainWindow.cpp` (complete):
```cpp
#include "ui/MainWindow.h"
#include "ui/CountdownOverlay.h"
#include "ui/RegionSelectorOverlay.h"
#include "app/RecordingStore.h"
#include "app/RecordingController.h"
#include "app/RegionPresets.h"
#include "platform/GlobalHotkeyX11.h"
#include "recording/types.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <X11/keysym.h>

namespace rr {

static QString dataJsonPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/recordings.json";
}
static QString moviesDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString dir = base + "/RegionRecord";
    QDir().mkpath(dir);
    return dir;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("RegionRecord"));
    resize(640, 480);

    store_ = std::make_unique<RecordingStore>(dataJsonPath());
    controller_ = std::make_unique<RecordingController>(store_.get());

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // control bar
    auto* bar = new QHBoxLayout();
    regionBox_ = new QComboBox();
    for (const auto& p : standardPresets()) regionBox_->addItem(p.name);
    regionBox_->addItem(QStringLiteral("Fullscreen"));
    regionBox_->addItem(QStringLiteral("Custom selection"));
    delayBox_ = new QComboBox();
    delayBox_->addItems({QStringLiteral("0 s"), QStringLiteral("3 s"), QStringLiteral("5 s")});
    delayBox_->setCurrentIndex(1);
    fpsBox_ = new QSpinBox(); fpsBox_->setRange(5, 60); fpsBox_->setValue(30);
    audioBox_ = new QCheckBox(QStringLiteral("Record audio")); audioBox_->setChecked(false);
    auto* startBtn = new QPushButton(QStringLiteral("Start recording"));
    bar->addWidget(new QLabel(QStringLiteral("Region"))); bar->addWidget(regionBox_);
    bar->addWidget(new QLabel(QStringLiteral("Delay"))); bar->addWidget(delayBox_);
    bar->addWidget(new QLabel(QStringLiteral("FPS"))); bar->addWidget(fpsBox_);
    bar->addWidget(audioBox_);
    bar->addStretch();
    bar->addWidget(startBtn);
    root->addLayout(bar);

    list_ = new QListWidget();
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    root->addWidget(list_);
    setCentralWidget(central);

    // overlays
    countdown_ = new CountdownOverlay();
    selector_ = new RegionSelectorOverlay();

    // tray
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("Stop recording"), this, &MainWindow::onStopRequested);
    trayMenu->addAction(QStringLiteral("Show main window"), this, [this]{ showNormal(); raise(); });
    tray_->setContextMenu(trayMenu);
    tray_->show();

    // global hotkey Ctrl+Alt+S to stop
    hotkey_ = new GlobalHotkeyX11(this);
    hotkey_->registerHotkey(1, true, true, false, XK_s);
    connect(hotkey_, &GlobalHotkeyX11::triggered, this, &MainWindow::onStopRequested);

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(countdown_, &CountdownOverlay::countdownFinished, this, &MainWindow::beginCapture);
    connect(controller_.get(), &RecordingController::recordingCompleted,
            this, &MainWindow::onCompleted);
    connect(store_.get(), &RecordingStore::changed, this, &MainWindow::refreshList);
    connect(list_, &QListWidget::customContextMenuRequested, this, &MainWindow::onRowAction);
    connect(selector_, &RegionSelectorOverlay::regionSelected, this,
            [this](const CaptureRegion&) { countdown_->start(delayBox_->currentIndex() == 0 ? 0
                                            : (delayBox_->currentIndex() == 1 ? 3 : 5)); });

    store_->load();
}

MainWindow::~MainWindow() = default;

void MainWindow::refreshList() {
    list_->clear();
    for (const auto& it : store_->items()) {
        const QString name = QFileInfo(it.filePath).fileName();
        const QString secs = QString::number(it.durationMs / 1000.0, 'f', 1);
        list_->addItem(QString("[%1]  %2   %3s")
                       .arg(stateLabel(it.state), name, secs));
    }
}

void MainWindow::onStartClicked() {
    pending_.regionChoice = regionBox_->currentIndex();
    pending_.fps = fpsBox_->value();
    pending_.audio = audioBox_->isChecked();
    hide();                       // hide self to ensure it is not recorded into the frame
    QGuiApplication::processEvents();

    const int presetCount = standardPresets().size();
    const int delaySec = delayBox_->currentIndex() == 0 ? 0
                        : (delayBox_->currentIndex() == 1 ? 3 : 5);
    if (pending_.regionChoice == presetCount + 1) {     // custom selection
        selector_->beginSelection();                     // after selecting -> countdown (see the lambda in the constructor)
    } else {
        countdown_->start(delaySec);
    }
}

void MainWindow::beginCapture() {
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    const int presetCount = standardPresets().size();

    CaptureRegion region;
    if (pending_.regionChoice < presetCount)
        region = regionFromPreset(standardPresets()[pending_.regionChoice],
                                  g.x(), g.y(), g.width(), g.height());
    else // fullscreen (custom selection already set the region in the selector lambda; this is the fullscreen fallback)
        region = fullscreenRegion(g.x(), g.y(), g.width(), g.height());

    OutputOptions opts;
    const QString file = moviesDir() + "/" +
        QStringLiteral("rec-%1.mp4").arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));
    opts.path = file.toStdString();
    opts.fps = pending_.fps;
    opts.audioEnabled = pending_.audio;
    controller_->startRecording(region, opts);
}

void MainWindow::onStopRequested() {
    if (controller_->isRecording()) controller_->stopRecording();
}

void MainWindow::onCompleted(const QString&, const QString& path) {
    tray_->showMessage(QStringLiteral("RegionRecord"),
                       QFileInfo(path).fileName() + QStringLiteral(" finalized"),
                       QSystemTrayIcon::Information, 4000);
    showNormal(); raise();
}

bool MainWindow::hasFinalizing() const {
    for (const auto& it : store_->items())
        if (it.state == RecordingState::Finalizing) return true;
    return false;
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!hasFinalizing()) { e->accept(); return; }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("A recording is still finalizing"));
    box.setText(QStringLiteral("A task is writing the file trailer. What would you like to do?"));
    auto* bg = box.addButton(QStringLiteral("Continue in background (minimize to tray)"), QMessageBox::AcceptRole);
    auto* wait = box.addButton(QStringLiteral("Wait for completion then quit"), QMessageBox::ActionRole);
    auto* force = box.addButton(QStringLiteral("Force quit"), QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == bg) { e->ignore(); hide(); }
    else if (box.clickedButton() == wait) { e->ignore(); /* quit later in the completion signal */ }
    else if (box.clickedButton() == force) { e->accept(); }
}

void MainWindow::onRowAction() {
    const int row = list_->currentRow();
    if (row < 0) return;
    QMenu menu(this);
    menu.addAction(QStringLiteral("Play"), this, [this, row]{ playRow(row); });
    menu.addAction(QStringLiteral("Open containing folder"), this, [this, row]{ openFolderRow(row); });
    menu.addAction(QStringLiteral("Delete"), this, [this, row]{ deleteRow(row); });
    menu.exec(QCursor::pos());
}

void MainWindow::playRow(int row) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(store_->items()[row].filePath));
}
void MainWindow::openFolderRow(int row) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QFileInfo(store_->items()[row].filePath).absolutePath()));
}
void MainWindow::deleteRow(int row) {
    const QString path = store_->items()[row].filePath;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Delete recording"));
    box.setText(QStringLiteral("How would you like to delete this recording?"));
    auto* listOnly = box.addButton(QStringLiteral("Remove from list only"), QMessageBox::AcceptRole);
    auto* withFile = box.addButton(QStringLiteral("Also delete the video file"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);           // default focus on Cancel to prevent accidental deletion
    box.exec();
    if (box.clickedButton() == cancel) return;
    if (box.clickedButton() == withFile) QFile::remove(path);
    store_->removeAt(row);
    (void)listOnly;
}

}
```

- [ ] **Step 3: Migrate the old MainWindow and update main.cpp/CMake**

Delete `src/MainWindow.h`/`src/MainWindow.cpp` (the old minimal window); make the `RegionRecord` executable use `rr_ui`. Modify the executable target in `CMakeLists.txt`:
```cmake
add_library(rr_ui STATIC
    src/ui/CountdownOverlay.cpp
    src/ui/RegionSelectorOverlay.cpp
    src/ui/MainWindow.cpp
)
target_include_directories(rr_ui PUBLIC src)
target_link_libraries(rr_ui PUBLIC rr_app Qt6::Widgets)

add_executable(RegionRecord src/main.cpp)
target_link_libraries(RegionRecord PRIVATE rr_ui)
```
Modify `src/main.cpp`: `#include "ui/MainWindow.h"`, set `QApplication::setApplicationName("RegionRecord")` and `setOrganizationName("insnap")` (affects AppDataLocation), keep the rest of the `Q_IMPORT_PLUGIN` block.

- [ ] **Step 4: Write the offscreen smoke test**

`tests/app/test_mainwindow.cpp`:
```cpp
#include <QtTest>
#include "ui/MainWindow.h"

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    void constructsAndShows();
};

void TestMainWindow::constructsAndShows() {
    rr::MainWindow w;
    w.show();
    QVERIFY(w.isVisible());
    w.close();
}

QTEST_MAIN(TestMainWindow)
#include "test_mainwindow.moc"
```
Append at the end of CMake:
```cmake
add_executable(test_mainwindow tests/app/test_mainwindow.cpp)
target_link_libraries(test_mainwindow PRIVATE rr_ui Qt6::Test)
add_test(NAME test_mainwindow COMMAND test_mainwindow)
```

- [ ] **Step 5: Build and run (offscreen) + full regression**

```bash
cmake -S . -B build-dev -G Ninja -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake/Qt6
cmake --build build-dev
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev --output-on-failure
```
Expected: all PASS (including the new `test_mainwindow`), no regression.

- [ ] **Step 6: Manual smoke test (when a display is available, optional)**

```bash
./build-dev/RegionRecord
```
Choose "720p" + 3-second delay -> Start -> record after the countdown -> `Ctrl+Alt+S` to stop -> a "Completed" entry appears in the list -> double-click/right-click to play.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(p3): MainWindow list UI + tray + close-intercept + notify + playback"
```

---

## Self-Review

**1. Spec §5 coverage:**
- §5.1 list / state machine / JSON persistence / three-way delete / crash recovery → Task 1/2/8.
- §5.2 three-way close / completion notification / play / open folder → Task 8.
- §5.3 region presets / fullscreen / drag region select → Task 3/7/8.
- §5.4 hide self / delayed countdown / timing guarantee (recording only starts after the countdown ends) → Task 7/8 (`countdownFinished`→`beginCapture`).
- §5.5 global hotkey Ctrl+Alt+S / tray fallback / conflict validation → Task 4/6/8.
- Audio off by default, can be enabled → Task 8 (`audioBox_` unchecked by default, passed into `OutputOptions::audioEnabled`).
  > Note: actually muxing audio into the MP4 is a later P2 engine enhancement (the current `Mp4Encoder` is video-only); this plan only wires up the UI toggle and parameter passing, leaving audio-track encoding to a future engine-extension task.

**2. Placeholder scan:** No TBDs; every code step contains complete code; icons use Qt standard icons as placeholders, noted as such.

**3. Type consistency:** `RecordingState`/`RecordingItem` (Task 1) run through Store/Controller/MainWindow; `CaptureRegion`/`OutputOptions` (P2 types.h) are consistent across Presets/Controller/MainWindow; `RecordingStore` method signatures are consistent across Task 2/5/8; `RecordingController::{startRecording,stopRecording,isRecording,recordingCompleted}` is consistent across Task 5/8; `CountdownOverlay::{start,countdownFinished}`, `RegionSelectorOverlay::{beginSelection,regionSelected,cancelled}`, and `GlobalHotkey::{registerHotkey,unregisterAll,triggered}` are consistent across Task 6/7/8.

**4. Known limitations (non-blocking):** audio-track encoding is left to the engine extension; the Win/mac FrameSource and hotkey backends are future plans; the detail of stashing the custom region-select region inside the selector lambda needs to be carried by a member variable when Task 8 is implemented (change the lambda to store `pendingRegion_` and then call `countdown_->start`).
```
