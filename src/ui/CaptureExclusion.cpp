#include "ui/CaptureExclusion.h"

#include <QtGlobal>
#include <QWidget>

#if defined(Q_OS_WIN)
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace rr {

void excludeFromScreenCapture(QWidget* w) {
#if defined(Q_OS_WIN)
    if (!w)
        return;
    // winId() forces creation of the native window so the HWND is valid.
    HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (hwnd)
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
#else
    Q_UNUSED(w);
#endif
}

}  // namespace rr
