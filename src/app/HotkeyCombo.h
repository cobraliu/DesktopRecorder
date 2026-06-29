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
