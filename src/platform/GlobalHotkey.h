#pragma once
#include <QObject>
#include <memory>

namespace rr {
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~GlobalHotkey() override = default;
    // key is a platform-neutral Qt::Key value (e.g. Qt::Key_S); each backend maps it
    // to its native keycode. Only letter, digit and function keys need to be supported.
    virtual bool registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) = 0;
    virtual void unregisterAll() = 0;
signals:
    void triggered(int id);
};

// Creates the global-hotkey backend for the current platform, parented to `parent`.
// Returns nullptr on platforms without a backend yet (callers must null-check).
GlobalHotkey* createGlobalHotkey(QObject* parent);
}
