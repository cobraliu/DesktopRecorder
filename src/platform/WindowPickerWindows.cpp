#include "platform/WindowPickerWindows.h"

#include <windows.h>

namespace rr {

static int evenDown(int v) { return v - (v % 2); }

bool WindowPickerWindows::pickBlocking(CaptureRegion& out) {
    HCURSOR cross = LoadCursor(nullptr, IDC_CROSS);
    HCURSOR prev = SetCursor(cross);

    bool ok = false;
    for (;;) {
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) { ok = false; break; }
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            POINT pt;
            GetCursorPos(&pt);
            HWND hw = WindowFromPoint(pt);
            if (hw) hw = GetAncestor(hw, GA_ROOT);  // resolve to the top-level window
            RECT r;
            if (hw && GetWindowRect(hw, &r)) {
                const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
                const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
                const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                int x = r.left < vx ? vx : r.left;
                int y = r.top < vy ? vy : r.top;
                int w = r.right - r.left;
                int h = r.bottom - r.top;
                if (x + w > vx + vw) w = vx + vw - x;
                if (y + h > vy + vh) h = vy + vh - y;
                out.x = x;
                out.y = y;
                out.w = evenDown(w);
                out.h = evenDown(h);
                ok = (out.w >= 2 && out.h >= 2);
            }
            // Wait for release so the click does not keep registering.
            while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) Sleep(10);
            break;
        }
        Sleep(10);
    }

    SetCursor(prev);
    return ok;
}

}  // namespace rr
