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
