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
    bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) override;
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
