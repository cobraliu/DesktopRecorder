#pragma once
#include "platform/GlobalHotkey.h"
#include <QVector>
#include <atomic>
#include <future>
#include <thread>

namespace rr {

// Global hotkey via the Win32 RegisterHotKey API. RegisterHotKey with a null
// window routes WM_HOTKEY to the registering thread's message queue, so the
// registration and the GetMessage loop both run on a dedicated worker thread;
// the registration result is marshalled back to the caller via a future.
class GlobalHotkeyWindows : public GlobalHotkey {
    Q_OBJECT
public:
    explicit GlobalHotkeyWindows(QObject* parent = nullptr);
    ~GlobalHotkeyWindows() override;
    bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) override;
    void unregisterAll() override;

private:
    struct Binding { int id; unsigned int mods; unsigned int vk; };
    void runLoop(std::promise<bool> ready);

    std::thread thread_;
    unsigned long threadId_ = 0;  // DWORD set by the worker thread
    std::atomic<bool> running_{false};
    QVector<Binding> bindings_;
};

}  // namespace rr
