#pragma once
#include <QMainWindow>
#include <memory>
#include "recording/types.h"

class QListWidget;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QButtonGroup;
class QLabel;
class QSystemTrayIcon;

namespace rr {

class RecordingStore;
class RecordingController;
class CountdownOverlay;
class CaptureFrameWindow;
class GlobalHotkey;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void refreshList();
    void onModeClicked(int id);   // user clicks a segmented button
    void onStartClicked();
    void beginCapture();          // actually start recording after the countdown ends
    void onStopRequested();
    void onCompleted(const QString& id, const QString& path);

private:
    enum Mode { Fullscreen = 0, Region = 1, Window = 2 };

    int delaySeconds() const;
    bool hasFinalizing() const;
    void showEditingFrame();      // enter the editable floating frame at frameRegion_
    CaptureRegion defaultRegion() const;
    void buildStopHud();
    void showStopHud(const CaptureRegion& region, bool fullscreen);
    void hideStopHud();
    void playRow(int row);
    void openFolderRow(int row);   // open the file's containing directory (select the file if possible)
    void deleteRow(int row);

    std::unique_ptr<RecordingStore> store_;
    std::unique_ptr<RecordingController> controller_;
    CountdownOverlay* countdown_ = nullptr;
    CaptureFrameWindow* captureFrame_ = nullptr;
    GlobalHotkey* hotkey_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QWidget* stopHud_ = nullptr;          // persistent floating "Stop" button shown while recording (does not rely on a hotkey)
    QLabel* stopHudHint_ = nullptr;
    bool hotkeyOk_ = false;

    QListWidget* list_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QButtonGroup* modeGroup_ = nullptr;
    QComboBox* delayBox_ = nullptr;
    QSpinBox* fpsBox_ = nullptr;
    QCheckBox* audioBox_ = nullptr;
    QPushButton* recordBtn_ = nullptr;

    Mode mode_ = Region;
    CaptureRegion frameRegion_{};        // the region currently shown by the floating frame (region select / window pick)
    CaptureRegion savedRegionSelect_{};  // the last "region select" area, restored when switching back to region select

    // pending recording parameters (held during the countdown)
    int pendingFps_ = 30;
    bool pendingAudio_ = false;
    CaptureRegion pendingRegion_{};
    bool quitAfterFinalize_ = false;
};

}
