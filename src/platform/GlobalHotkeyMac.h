#pragma once
#include "platform/GlobalHotkey.h"
#include <QVector>

namespace rr {

// Global hotkey via the Carbon RegisterEventHotKey API. Carbon delivers
// kEventHotKeyPressed to a handler installed on the application event target, dispatched
// on the main run loop (which Qt drives via CFRunLoop), so no extra thread is needed.
// RegisterEventHotKey does not require the Accessibility permission (unlike CGEventTap).
class GlobalHotkeyMac : public GlobalHotkey {
    Q_OBJECT
public:
    explicit GlobalHotkeyMac(QObject* parent = nullptr);
    ~GlobalHotkeyMac() override;
    bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) override;
    void unregisterAll() override;

    // Called by the file-internal Carbon event handler when a registered hotkey fires.
    void fire(int id) { emit triggered(id); }

private:
    struct Binding { int id; void* ref; };  // ref = EventHotKeyRef
    QVector<Binding> bindings_;
    void* handler_ = nullptr;  // EventHandlerRef, installed lazily on first registration
    void* handlerUPP_ = nullptr;  // EventHandlerUPP, disposed in unregisterAll
};

}  // namespace rr
