#include "platform/GlobalHotkeyWindows.h"

#include <windows.h>

namespace rr {

// Map a platform-neutral Qt::Key to a Win32 virtual-key code for the keys we support.
// VK codes for letters/digits equal their ASCII uppercase value; F-keys are sequential.
static unsigned int qtKeyToVk(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<unsigned>('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<unsigned>('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return static_cast<unsigned>(VK_F1 + (key - Qt::Key_F1));
    return 0;
}

GlobalHotkeyWindows::GlobalHotkeyWindows(QObject* parent) : GlobalHotkey(parent) {}

GlobalHotkeyWindows::~GlobalHotkeyWindows() { unregisterAll(); }

bool GlobalHotkeyWindows::registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) {
    const unsigned int vk = qtKeyToVk(key);
    if (vk == 0) return false;
    // The app registers a single hotkey; a second registration while the loop is
    // running is not supported (would require cross-thread RegisterHotKey marshalling).
    if (running_.load()) return false;

    unsigned int mods = MOD_NOREPEAT;
    if (ctrl)  mods |= MOD_CONTROL;
    if (alt)   mods |= MOD_ALT;
    if (shift) mods |= MOD_SHIFT;
    bindings_.push_back({id, mods, vk});

    std::promise<bool> ready;
    std::future<bool> fut = ready.get_future();
    running_.store(true);
    thread_ = std::thread([this, r = std::move(ready)]() mutable { runLoop(std::move(r)); });
    const bool ok = fut.get();  // whether RegisterHotKey succeeded on the worker thread
    if (!ok) {
        // The worker exits on failure (see runLoop); reap it and drop the dead
        // binding so a later registration attempt starts clean instead of being
        // rejected by the running_ guard above.
        if (thread_.joinable()) thread_.join();
        threadId_ = 0;
        bindings_.clear();
    }
    return ok;
}

void GlobalHotkeyWindows::runLoop(std::promise<bool> ready) {
    threadId_ = GetCurrentThreadId();
    bool ok = true;
    for (const auto& b : bindings_)
        if (!RegisterHotKey(nullptr, b.id, b.mods, b.vk)) ok = false;
    if (!ok) {
        // Undo any partial registration and exit; parking in GetMessage with
        // running_ latched true would block every future registerHotkey call.
        for (const auto& b : bindings_) UnregisterHotKey(nullptr, b.id);
        running_.store(false);
        ready.set_value(false);
        return;
    }
    ready.set_value(true);

    MSG msg;
    while (running_.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY)
            emit triggered(static_cast<int>(msg.wParam));
    }

    for (const auto& b : bindings_) UnregisterHotKey(nullptr, b.id);
}

void GlobalHotkeyWindows::unregisterAll() {
    if (running_.load()) {
        running_.store(false);
        // Wake the blocking GetMessage so the loop can observe running_ == false.
        if (threadId_) PostThreadMessage(threadId_, WM_NULL, 0, 0);
        if (thread_.joinable()) thread_.join();
        threadId_ = 0;
    }
    bindings_.clear();
}

}  // namespace rr
