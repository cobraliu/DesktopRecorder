#include "app/HotkeyCombo.h"
#include <Qt>

namespace rr {

bool isAcceptableHotkey(const HotkeyCombo& c, QString* reason) {
    auto fail = [&](const QString& m) { if (reason) *reason = m; return false; };

    if (c.meta)
        return fail(QStringLiteral("Win/Cmd key combinations are not allowed; they easily conflict with the system"));
    if (!c.ctrl && !c.alt && !c.shift)
        return fail(QStringLiteral("Must include at least one modifier key (Ctrl/Alt/Shift)"));
    if (c.key == 0 || c.key == Qt::Key_Print || c.key == Qt::Key_Escape)
        return fail(QStringLiteral("This function key cannot be used"));

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
