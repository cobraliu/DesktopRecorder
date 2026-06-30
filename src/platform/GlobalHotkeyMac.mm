#include "platform/GlobalHotkeyMac.h"

#include <Carbon/Carbon.h>

namespace rr {

namespace {

constexpr OSType kHotkeySignature = 'RgRc';
constexpr UInt32 kInvalidVk = 0xFFFFFFFF;

// Map a platform-neutral Qt::Key to a macOS virtual key code. kVK_ANSI_A is 0, so an
// invalid key must be signalled with a dedicated sentinel rather than 0.
UInt32 qtKeyToVk(int key) {
    switch (key) {
        case Qt::Key_A: return kVK_ANSI_A;
        case Qt::Key_B: return kVK_ANSI_B;
        case Qt::Key_C: return kVK_ANSI_C;
        case Qt::Key_D: return kVK_ANSI_D;
        case Qt::Key_E: return kVK_ANSI_E;
        case Qt::Key_F: return kVK_ANSI_F;
        case Qt::Key_G: return kVK_ANSI_G;
        case Qt::Key_H: return kVK_ANSI_H;
        case Qt::Key_I: return kVK_ANSI_I;
        case Qt::Key_J: return kVK_ANSI_J;
        case Qt::Key_K: return kVK_ANSI_K;
        case Qt::Key_L: return kVK_ANSI_L;
        case Qt::Key_M: return kVK_ANSI_M;
        case Qt::Key_N: return kVK_ANSI_N;
        case Qt::Key_O: return kVK_ANSI_O;
        case Qt::Key_P: return kVK_ANSI_P;
        case Qt::Key_Q: return kVK_ANSI_Q;
        case Qt::Key_R: return kVK_ANSI_R;
        case Qt::Key_S: return kVK_ANSI_S;
        case Qt::Key_T: return kVK_ANSI_T;
        case Qt::Key_U: return kVK_ANSI_U;
        case Qt::Key_V: return kVK_ANSI_V;
        case Qt::Key_W: return kVK_ANSI_W;
        case Qt::Key_X: return kVK_ANSI_X;
        case Qt::Key_Y: return kVK_ANSI_Y;
        case Qt::Key_Z: return kVK_ANSI_Z;
        case Qt::Key_0: return kVK_ANSI_0;
        case Qt::Key_1: return kVK_ANSI_1;
        case Qt::Key_2: return kVK_ANSI_2;
        case Qt::Key_3: return kVK_ANSI_3;
        case Qt::Key_4: return kVK_ANSI_4;
        case Qt::Key_5: return kVK_ANSI_5;
        case Qt::Key_6: return kVK_ANSI_6;
        case Qt::Key_7: return kVK_ANSI_7;
        case Qt::Key_8: return kVK_ANSI_8;
        case Qt::Key_9: return kVK_ANSI_9;
        case Qt::Key_F1: return kVK_F1;
        case Qt::Key_F2: return kVK_F2;
        case Qt::Key_F3: return kVK_F3;
        case Qt::Key_F4: return kVK_F4;
        case Qt::Key_F5: return kVK_F5;
        case Qt::Key_F6: return kVK_F6;
        case Qt::Key_F7: return kVK_F7;
        case Qt::Key_F8: return kVK_F8;
        case Qt::Key_F9: return kVK_F9;
        case Qt::Key_F10: return kVK_F10;
        case Qt::Key_F11: return kVK_F11;
        case Qt::Key_F12: return kVK_F12;
        default: return kInvalidVk;
    }
}

OSStatus hotKeyHandler(EventHandlerCallRef, EventRef event, void* userData) {
    EventHotKeyID hk;
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(hk), nullptr, &hk) == noErr) {
        if (auto* self = static_cast<GlobalHotkeyMac*>(userData)) self->fire(static_cast<int>(hk.id));
    }
    return noErr;
}

}  // namespace

GlobalHotkeyMac::GlobalHotkeyMac(QObject* parent) : GlobalHotkey(parent) {}

GlobalHotkeyMac::~GlobalHotkeyMac() { unregisterAll(); }

bool GlobalHotkeyMac::registerHotkey(int id, bool ctrl, bool alt, bool shift, int key) {
    const UInt32 vk = qtKeyToVk(key);
    if (vk == kInvalidVk) return false;

    if (!handler_) {
        EventTypeSpec spec = {kEventClassKeyboard, kEventHotKeyPressed};
        EventHandlerUPP upp = NewEventHandlerUPP(hotKeyHandler);
        EventHandlerRef ref = nullptr;
        if (InstallEventHandler(GetApplicationEventTarget(), upp, 1, &spec, this, &ref) != noErr) {
            DisposeEventHandlerUPP(upp);
            return false;
        }
        handler_ = ref;
        handlerUPP_ = reinterpret_cast<void (*)()>(upp);
    }

    // Map the app's cross-platform Ctrl modifier to the Carbon control key. cmdKey is
    // available if a Command-based combo is ever wanted.
    UInt32 mods = 0;
    if (ctrl) mods |= controlKey;
    if (alt) mods |= optionKey;
    if (shift) mods |= shiftKey;

    EventHotKeyID hkId;
    hkId.signature = kHotkeySignature;
    hkId.id = static_cast<UInt32>(id);
    EventHotKeyRef hk = nullptr;
    if (RegisterEventHotKey(vk, mods, hkId, GetApplicationEventTarget(), 0, &hk) != noErr)
        return false;

    bindings_.push_back({id, hk});
    return true;
}

void GlobalHotkeyMac::unregisterAll() {
    for (const auto& b : bindings_)
        if (b.ref) UnregisterEventHotKey(static_cast<EventHotKeyRef>(b.ref));
    bindings_.clear();
    if (handler_) {
        RemoveEventHandler(static_cast<EventHandlerRef>(handler_));
        handler_ = nullptr;
    }
    if (handlerUPP_) {
        DisposeEventHandlerUPP(reinterpret_cast<EventHandlerUPP>(handlerUPP_));
        handlerUPP_ = nullptr;
    }
}

}  // namespace rr
