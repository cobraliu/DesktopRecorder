#include "ui/MainWindow.h"
#include "ui/CountdownOverlay.h"
#include "ui/CaptureFrameWindow.h"
#include "ui/CaptureExclusion.h"
#include "ui/icons.h"
#include "app/RecordingStore.h"
#include "app/RecordingController.h"
#include "app/RegionPresets.h"
#include "platform/GlobalHotkey.h"
#include "platform/WindowPicker.h"
#include "recording/types.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QFrame>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QColor>

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

// Unified stroke color for the small in-row icons
static const QColor kRowIcon(0xb4, 0xbe, 0xc8);

// state -> (badge text, semantic color)
struct StateVisual { QString text; QColor color; };
static StateVisual stateVisual(RecordingState s) {
    switch (s) {
        case RecordingState::Recording:               return {QStringLiteral("Recording"),    QColor(0xe5, 0x48, 0x4d)};
        case RecordingState::Finalizing:              return {QStringLiteral("Finalizing"),   QColor(0xd6, 0x8a, 0x1e)};
        case RecordingState::Completed:               return {QStringLiteral("Completed"),    QColor(0x2f, 0xb0, 0x7a)};
        case RecordingState::FinalizationInterrupted: return {QStringLiteral("Finalize interrupted"), QColor(0xcf, 0x7a, 0x2e)};
        case RecordingState::Failed:                  return {QStringLiteral("Failed"),       QColor(0x9e, 0x4a, 0x4a)};
    }
    return {QString(), QColor(0x55, 0x55, 0x55)};
}

static QString relativeTime(qint64 ms) {
    if (ms <= 0) return QString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 d = (now - ms) / 1000;
    if (d < 60)    return QStringLiteral("just now");
    if (d < 3600)  return QStringLiteral("%1 min ago").arg(d / 60);
    if (d < 86400) return QStringLiteral("%1 hr ago").arg(d / 3600);
    return QStringLiteral("%1 days ago").arg(d / 86400);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("RegionRecord"));
    resize(560, 620);

    store_ = std::make_unique<RecordingStore>(dataJsonPath());
    controller_ = std::make_unique<RecordingController>(store_.get());
    frameRegion_ = defaultRegion();
    savedRegionSelect_ = frameRegion_;

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);

    // ---- Control card ----
    auto* card = new QFrame();
    card->setObjectName(QStringLiteral("controlCard"));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(16, 16, 16, 16);
    cardLay->setSpacing(12);

    // Segmented three modes
    auto* modeRow = new QHBoxLayout();
    modeRow->setSpacing(8);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    struct ModeDef { int id; const char* label; QIcon (*icon)(const QColor&, int); };
    const ModeDef defs[] = {
        {Fullscreen, "Fullscreen",   &iconFullscreen},
        {Region,     "Region",       &iconFrame},
        {Window,     "Window",       &iconWindow}};
    for (const auto& d : defs) {
        auto* b = new QPushButton(QStringLiteral("  ") + QString::fromUtf8(d.label));
        b->setCheckable(true);
        b->setProperty("segmented", true);
        b->setIcon(d.icon(kRowIcon, 18));
        b->setIconSize(QSize(18, 18));
        b->setCursor(Qt::PointingHandCursor);
        modeGroup_->addButton(b, d.id);
        modeRow->addWidget(b);
    }
    if (auto* b = modeGroup_->button(mode_)) b->setChecked(true);
    cardLay->addLayout(modeRow);

    // Options row: delay / FPS / audio
    auto* optRow = new QHBoxLayout();
    optRow->setSpacing(8);
    delayBox_ = new QComboBox();
    delayBox_->addItems({QStringLiteral("No delay"), QStringLiteral("3 sec delay"),
                         QStringLiteral("5 sec delay")});
    delayBox_->setCurrentIndex(1);
    fpsBox_ = new QSpinBox(); fpsBox_->setRange(5, 60); fpsBox_->setValue(10);
    fpsBox_->setSuffix(QStringLiteral(" FPS"));
    audioBox_ = new QCheckBox(QStringLiteral("Record audio"));
    optRow->addWidget(delayBox_);
    optRow->addWidget(fpsBox_);
    optRow->addWidget(audioBox_);
    optRow->addStretch();
    cardLay->addLayout(optRow);

    recordBtn_ = new QPushButton(QStringLiteral("  Start recording"));
    recordBtn_->setObjectName(QStringLiteral("recordBtn"));
    recordBtn_->setIcon(iconRecord(QColor(0x06, 0x12, 0x0f), 14));
    recordBtn_->setIconSize(QSize(14, 14));
    recordBtn_->setCursor(Qt::PointingHandCursor);
    cardLay->addWidget(recordBtn_);

    // Real depth for the control card (QSS has no box-shadow, so use a graphics effect)
    auto* cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setBlurRadius(28);
    cardShadow->setColor(QColor(0, 0, 0, 140));
    cardShadow->setOffset(0, 6);
    card->setGraphicsEffect(cardShadow);

    root->addWidget(card);

    // ---- History area ----
    auto* histLabel = new QLabel(QStringLiteral("Recording history"));
    histLabel->setObjectName(QStringLiteral("sectionLabel"));
    root->addWidget(histLabel);

    auto* stack = new QStackedLayout();
    stack->setStackingMode(QStackedLayout::StackOne);
    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    emptyHint_ = new QLabel(QStringLiteral("No recordings yet\nPick a region, then click \"Start recording\" above"));
    emptyHint_->setObjectName(QStringLiteral("emptyHint"));
    emptyHint_->setAlignment(Qt::AlignCenter);
    stack->addWidget(list_);
    stack->addWidget(emptyHint_);
    auto* stackHost = new QWidget();
    stackHost->setLayout(stack);
    root->addWidget(stackHost, 1);

    setCentralWidget(central);

    // overlays / floating frame
    countdown_ = new CountdownOverlay(this);
    captureFrame_ = new CaptureFrameWindow();   // top-level standalone window

    // Tray
    tray_ = new QSystemTrayIcon(this);
    tray_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction(QStringLiteral("Stop recording"), this, &MainWindow::onStopRequested);
    trayMenu->addAction(QStringLiteral("Show main window"), this, [this]{ showNormal(); raise(); });
    tray_->setContextMenu(trayMenu);
    tray_->show();

    // Persistent floating "Stop" button shown while recording (does not rely on the global hotkey, always available)
    buildStopHud();

    // Global hotkey Ctrl+Alt+S to stop (may be taken by the desktop environment / another program; the registration result decides the hint text)
    hotkey_ = createGlobalHotkey(this);
    hotkeyOk_ = hotkey_ && hotkey_->registerHotkey(1, true, true, false, Qt::Key_S);
    if (hotkey_)
        connect(hotkey_, &GlobalHotkey::triggered, this, &MainWindow::onStopRequested);
    countdown_->setHotkeyAvailable(hotkeyOk_);

    connect(recordBtn_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(modeGroup_, &QButtonGroup::idClicked, this, &MainWindow::onModeClicked);
    connect(countdown_, &CountdownOverlay::countdownFinished, this, &MainWindow::beginCapture);
    connect(controller_.get(), &RecordingController::recordingCompleted,
            this, &MainWindow::onCompleted);
    connect(controller_.get(), &RecordingController::recordingFailed,
            this, &MainWindow::onFailed);
    connect(store_.get(), &RecordingStore::changed, this, &MainWindow::refreshList);

    store_->load();
    refreshList();
    // Default region-select mode: show a draggable floating frame from the start
    showEditingFrame();
}

MainWindow::~MainWindow() {
    delete captureFrame_;   // top-level window, not part of the QObject parent tree, so release it manually
}

CaptureRegion MainWindow::defaultRegion() const {
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
    CaptureRegion r;
    // Screen-centered, 1/4 of the area (half width x half height), aligned to even values
    r.w = (g.width()  / 2) & ~1;
    r.h = (g.height() / 2) & ~1;
    r.x = g.x() + (g.width()  - r.w) / 2;
    r.y = g.y() + (g.height() - r.h) / 2;
    return r;
}

void MainWindow::showEditingFrame() {
    captureFrame_->beginEditing(frameRegion_);
}

void MainWindow::buildStopHud() {
    stopHud_ = new QWidget(nullptr,
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    stopHud_->setObjectName(QStringLiteral("stopHud"));
    stopHud_->setAttribute(Qt::WA_TranslucentBackground, true);
    stopHud_->setAttribute(Qt::WA_ShowWithoutActivating, true);

    // Outer padding to leave room for shadow rendering
    auto* outer = new QVBoxLayout(stopHud_);
    outer->setContentsMargins(18, 18, 18, 18);

    auto* panel = new QFrame(stopHud_);
    panel->setObjectName(QStringLiteral("stopHudPanel"));
    outer->addWidget(panel);

    auto* lay = new QHBoxLayout(panel);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(12);

    auto* btn = new QPushButton(QStringLiteral("  Stop recording"));
    btn->setObjectName(QStringLiteral("stopHudBtn"));
    btn->setIcon(iconStop(QColor(0xff, 0xff, 0xff), 14));
    btn->setIconSize(QSize(14, 14));
    btn->setCursor(Qt::PointingHandCursor);
    connect(btn, &QPushButton::clicked, this, &MainWindow::onStopRequested);
    lay->addWidget(btn);

    stopHudHint_ = new QLabel();
    stopHudHint_->setObjectName(QStringLiteral("stopHudHint"));
    lay->addWidget(stopHudHint_);

    auto* sh = new QGraphicsDropShadowEffect(panel);
    sh->setBlurRadius(24);
    sh->setColor(QColor(0, 0, 0, 160));
    sh->setOffset(0, 4);
    panel->setGraphicsEffect(sh);

    stopHud_->hide();
}

void MainWindow::showStopHud(const CaptureRegion& region, bool fullscreen) {
    if (!stopHud_) return;
    stopHudHint_->setText(hotkeyOk_
        ? QStringLiteral("or press Ctrl+Alt+S")
        : QStringLiteral("(global hotkey is taken)"));

    stopHud_->adjustSize();
    const QSize sz = stopHud_->size();
    // Clamp within the screen actually containing the recorded region, so the
    // HUD does not jump to the primary monitor on multi-screen setups.
    QScreen* screen = QGuiApplication::screenAt(
        QPoint(region.x + region.w / 2, region.y + region.h / 2));
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect sg = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const int margin = 16;
    int x, y;
    if (fullscreen) {
        // Fullscreen: bottom-right corner (can't fully avoid the recorded area; try not to block content)
        x = sg.right() - sz.width() - margin;
        y = sg.bottom() - sz.height() - margin;
    } else {
        // Region select / window pick: hug the outside of the recorded area to avoid being captured or blocking content
        const QRect cap(region.x, region.y, region.w, region.h);
        x = cap.center().x() - sz.width() / 2;
        y = cap.bottom() + 12 - 18;                 // subtract the outer shadow padding
        if (y + sz.height() > sg.bottom())          // doesn't fit below -> place above
            y = cap.top() - sz.height() + 18 - 12;
        if (y < sg.top())                           // still out of bounds -> bottom of the screen
            y = sg.bottom() - sz.height() - margin;
        x = qBound(sg.left() + margin, x, sg.right() - sz.width() - margin);
    }
    stopHud_->move(x, y);
    stopHud_->show();
    stopHud_->raise();
    // Keep the HUD visible to the user but out of the recording (Windows).
    excludeFromScreenCapture(stopHud_);
}

void MainWindow::hideStopHud() {
    if (stopHud_) stopHud_->hide();
}

int MainWindow::delaySeconds() const {
    switch (delayBox_->currentIndex()) {
        case 0: return 0;
        case 2: return 5;
        default: return 3;
    }
}

void MainWindow::onModeClicked(int id) {
    const Mode m = static_cast<Mode>(id);
    // Re-clicking the current mode changes nothing (and must not reset the
    // region frame to the stale saved one); Window re-picks on purpose.
    if (m == mode_ && m != Window) return;
    // When leaving "region select", remember the current frame so it can be restored on switching back
    if (mode_ == Region && m != Region)
        savedRegionSelect_ = captureFrame_->captureRegion();

    if (m == Fullscreen) {
        mode_ = m;
        captureFrame_->hide();
    } else if (m == Region) {
        mode_ = m;
        // Switching back to region select from another mode: restore the last selected region
        frameRegion_ = savedRegionSelect_;
        showEditingFrame();
    } else {   // Window: blocking click-to-pick
        hide();
        QGuiApplication::processEvents();
        auto picker = createWindowPicker();
        CaptureRegion picked{};
        const bool ok = picker && picker->pickBlocking(picked);
        showNormal(); raise();
        if (ok) {
            mode_ = m;
            frameRegion_ = picked;
            showEditingFrame();
        } else {
            // Cancelled: revert to the previous mode button (idClicked is not re-emitted by setChecked)
            if (auto* b = modeGroup_->button(mode_)) b->setChecked(true);
            // A silent revert is fine for Esc, but explain when there is no
            // picker at all (e.g. Wayland sessions).
            if (!picker)
                QMessageBox::information(this, QStringLiteral("Window pick unavailable"),
                    QStringLiteral("Picking a window is not supported on this session. "
                                   "Use region select to frame the window instead."));
        }
    }
}

void MainWindow::onStartClicked() {
    if (controller_->isRecording() || countdown_->isCounting()) return;
    pendingFps_ = fpsBox_->value();
    pendingAudio_ = audioBox_->isChecked();

    QScreen* screen = nullptr;
    if (mode_ == Fullscreen) {
        screen = QGuiApplication::primaryScreen();
        const QRect g = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);
        pendingRegion_ = fullscreenRegion(g.x(), g.y(), g.width(), g.height());
        pendingRegion_.dpiScale = screen ? screen->devicePixelRatio() : 1.0;
        captureFrame_->hide();
    } else {
        frameRegion_ = captureFrame_->captureRegion();
        // Qt geometry is logical; the X11/Windows frame sources need the ratio
        // to reach physical screen pixels on HiDPI displays.
        screen = captureFrame_->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        frameRegion_.dpiScale = screen ? screen->devicePixelRatio() : 1.0;
        pendingRegion_ = frameRegion_;
        captureFrame_->enterRecordingStyle();   // keep the red frame visible to mark that this area is being recorded
        // Re-apply after the flag change above, which can recreate the native window (Windows).
        excludeFromScreenCapture(captureFrame_);
    }

    setControlsEnabled(false);
    hide();                       // hide the main window so it isn't captured
    QGuiApplication::processEvents();
    countdown_->start(delaySeconds(), screen);   // show the countdown on the recorded screen
}

void MainWindow::beginCapture() {
    countdown_->hide();
    QGuiApplication::processEvents();

    OutputOptions opts;
    const QString file = moviesDir() + "/" +
        QStringLiteral("rec-%1.mp4").arg(
            QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));
    opts.path = file.toStdString();
    opts.fps = pendingFps_;
    opts.audioEnabled = pendingAudio_;
    controller_->startRecording(pendingRegion_, opts);
    showStopHud(pendingRegion_, mode_ == Fullscreen);
}

void MainWindow::onStopRequested() {
    // Stop during the countdown aborts the pending recording and restores the UI.
    if (countdown_->isCounting()) {
        countdown_->cancel();
        setControlsEnabled(true);
        showNormal(); raise();
        if (mode_ == Fullscreen) captureFrame_->hide();
        else showEditingFrame();
        return;
    }
    hideStopHud();
    if (controller_->isRecording()) controller_->stopRecording();
}

void MainWindow::onCompleted(const QString&, const QString& path) {
    hideStopHud();
    setControlsEnabled(true);
    tray_->showMessage(QStringLiteral("RegionRecord"),
                       QFileInfo(path).fileName() + QStringLiteral(" finalized"),
                       QSystemTrayIcon::Information, 4000);
    if (quitAfterFinalize_ && !hasFinalizing()) { close(); return; }
    showNormal(); raise();
    // Restore the floating frame: region select / window pick return to editing state, fullscreen hides it
    if (mode_ == Fullscreen) captureFrame_->hide();
    else showEditingFrame();
}

void MainWindow::onFailed(const QString&, const QString& msg) {
    // The recorder can fail at any point after Start (capture source rejected, output
    // file not writable, write error mid-recording). By then the main window is hidden
    // and the stop HUD / red frame are up; without this cleanup they stay stranded on
    // screen with the UI stuck in a "recording" look.
    hideStopHud();
    countdown_->cancel();
    setControlsEnabled(true);
    if (mode_ == Fullscreen) captureFrame_->hide();
    else showEditingFrame();
    showNormal(); raise();
    QMessageBox::warning(this, QStringLiteral("Recording failed"), msg);
}

bool MainWindow::hasFinalizing() const {
    for (const auto& it : store_->items())
        if (it.state == RecordingState::Finalizing) return true;
    return false;
}

void MainWindow::setControlsEnabled(bool on) {
    recordBtn_->setEnabled(on);
    delayBox_->setEnabled(on);
    fpsBox_->setEnabled(on);
    audioBox_->setEnabled(on);
    for (auto* b : modeGroup_->buttons()) b->setEnabled(on);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (controller_->isRecording()) {
        const auto r = QMessageBox::question(this,
            QStringLiteral("Recording in progress"),
            QStringLiteral("A recording is running. Stop it and quit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) { e->ignore(); return; }
        hideStopHud();
        controller_->stopRecording();
        quitAfterFinalize_ = true;   // onCompleted closes once the file is finalized
        e->ignore();
        return;
    }
    if (quitAfterFinalize_ || !hasFinalizing()) {
        if (captureFrame_) captureFrame_->hide();
        e->accept();
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("A recording is still finalizing"));
    box.setText(QStringLiteral("A task is still writing the file trailer. What would you like to do?"));
    auto* bg = box.addButton(QStringLiteral("Continue in background (minimize to tray)"), QMessageBox::AcceptRole);
    auto* wait = box.addButton(QStringLiteral("Wait for completion, then quit"), QMessageBox::ActionRole);
    auto* force = box.addButton(QStringLiteral("Force quit"), QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == bg) { e->ignore(); hide(); }
    else if (box.clickedButton() == wait) { e->ignore(); quitAfterFinalize_ = true; }
    else if (box.clickedButton() == force) { e->accept(); }
}

void MainWindow::refreshList() {
    list_->clear();
    const auto& items = store_->items();
    emptyHint_->setVisible(items.isEmpty());
    list_->setVisible(!items.isEmpty());

    for (int row = 0; row < items.size(); ++row) {
        const RecordingItem& it = items[row];
        const StateVisual sv = stateVisual(it.state);

        auto* cardW = new QFrame();
        cardW->setObjectName(QStringLiteral("card"));
        auto* h = new QHBoxLayout(cardW);
        h->setContentsMargins(12, 10, 12, 10);
        h->setSpacing(10);

        auto* dot = new QLabel();
        dot->setPixmap(statusDot(sv.color, 12));
        dot->setFixedWidth(14);
        dot->setAlignment(Qt::AlignCenter);
        h->addWidget(dot);

        auto* texts = new QVBoxLayout();
        texts->setSpacing(2);
        auto* title = new QLabel();
        title->setObjectName(QStringLiteral("cardTitle"));
        const QString name = QFileInfo(it.filePath).fileName();
        title->setText(title->fontMetrics().elidedText(name, Qt::ElideMiddle, 240));
        title->setToolTip(it.filePath);
        const QString secs = QString::number(it.durationMs / 1000.0, 'f', 1);
        QString meta = QStringLiteral("%1s").arg(secs);
        const QString rel = relativeTime(it.createdAtMs);
        if (!rel.isEmpty()) meta += QStringLiteral("  ·  ") + rel;
        auto* metaL = new QLabel(meta);
        metaL->setObjectName(QStringLiteral("cardMeta"));
        texts->addWidget(title);
        texts->addWidget(metaL);
        h->addLayout(texts, 1);

        // Semi-transparent semantic-tinted badge (easier on the eyes than a solid block on a dark panel)
        auto* badge = new QLabel(sv.text);
        badge->setStyleSheet(QStringLiteral(
            "color:%1; background:rgba(%2,%3,%4,40); border:1px solid rgba(%2,%3,%4,90); "
            "border-radius:8px; padding:2px 9px; font-size:11px; font-weight:600;")
            .arg(sv.color.name())
            .arg(sv.color.red()).arg(sv.color.green()).arg(sv.color.blue()));
        h->addWidget(badge);

        const bool playable = (it.state == RecordingState::Completed ||
                               it.state == RecordingState::FinalizationInterrupted);
        if (playable) {
            auto* play = new QPushButton();
            play->setProperty("rowbtn", true);
            play->setIcon(iconPlay(kRowIcon, 16));
            play->setIconSize(QSize(16, 16));
            play->setCursor(Qt::PointingHandCursor);
            play->setToolTip(QStringLiteral("Play"));
            connect(play, &QPushButton::clicked, this, [this, row]{ playRow(row); });
            h->addWidget(play);
        }
        auto* folder = new QPushButton();
        folder->setProperty("rowbtn", true);
        folder->setIcon(iconFolder(kRowIcon, 16));
        folder->setIconSize(QSize(16, 16));
        folder->setCursor(Qt::PointingHandCursor);
        folder->setToolTip(QStringLiteral("Open file location"));
        connect(folder, &QPushButton::clicked, this, [this, row]{ openFolderRow(row); });
        h->addWidget(folder);

        auto* del = new QPushButton();
        del->setProperty("rowbtn", true);
        del->setIcon(iconTrash(kRowIcon, 16));
        del->setIconSize(QSize(16, 16));
        del->setCursor(Qt::PointingHandCursor);
        del->setToolTip(QStringLiteral("Delete"));
        connect(del, &QPushButton::clicked, this, [this, row]{ deleteRow(row); });
        h->addWidget(del);

        auto* item = new QListWidgetItem(list_);
        item->setSizeHint(QSize(0, 60));
        list_->setItemWidget(item, cardW);
    }
}

void MainWindow::playRow(int row) {
    if (row < 0 || row >= store_->items().size()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(store_->items()[row].filePath));
}

void MainWindow::openFolderRow(int row) {
    if (row < 0 || row >= store_->items().size()) return;
    const QFileInfo fi(store_->items()[row].filePath);
    const QString dir = fi.exists() ? fi.absolutePath() : moviesDir();
#if defined(Q_OS_WIN)
    if (fi.exists()) {
        QProcess::startDetached(QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,") + QDir::toNativeSeparators(fi.absoluteFilePath())});
        return;
    }
#elif defined(Q_OS_MACOS)
    if (fi.exists()) {
        QProcess::startDetached(QStringLiteral("open"),
            {QStringLiteral("-R"), fi.absoluteFilePath()});
        return;
    }
#endif
    // Linux etc.: open the containing directory (file managers have no unified "select file" argument, so opening the directory is most reliable)
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::deleteRow(int row) {
    if (row < 0 || row >= store_->items().size()) return;
    const QString path = store_->items()[row].filePath;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Delete recording"));
    box.setText(QStringLiteral("How would you like to delete this recording?"));
    box.addButton(QStringLiteral("Remove from list only"), QMessageBox::AcceptRole);
    auto* withFile = box.addButton(QStringLiteral("Also delete the video file"), QMessageBox::DestructiveRole);
    auto* cancel = box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    if (box.clickedButton() == cancel) return;
    if (box.clickedButton() == withFile) QFile::remove(path);
    store_->removeAt(row);
}

}
