#include "ui/theme.h"

namespace rr {

// Design tokens (based on ui-ux-pro-max "Dark Mode (OLED)" + teal accent):
//   bg #15181d · control card #1f242b · input #272d35 · history card #1b2027 (hover #20262e)
//   primary text #eef1f4 · secondary text #97a2ad · border #2b323b
//   accent #19c3a3 (hover #25d4b2 / pressed #14a98c / on #06120f) · danger #e5484d
// Inter for a technical feel, with a Chinese fallback (Inter has no CJK glyphs).
QString darkTealStyleSheet() {
    return QStringLiteral(R"(
* {
    font-family: "Inter", "Segoe UI", "Noto Sans", "Source Han Sans SC",
                 "Noto Sans CJK SC", "Microsoft YaHei", sans-serif;
    font-size: 13px;
}

QMainWindow, QWidget#central { background: #15181d; color: #eef1f4; }

QLabel { color: #eef1f4; background: transparent; }
QLabel#sectionLabel {
    color: #97a2ad; font-size: 11px; font-weight: 700;
    letter-spacing: 1px; text-transform: uppercase; padding: 2px;
}
QLabel#emptyHint { color: #6b7682; font-size: 14px; line-height: 1.6; }

/* Control card: raised surface (real depth provided by QGraphicsDropShadowEffect) */
QFrame#controlCard {
    background: #1f242b;
    border: 1px solid #2b323b;
    border-radius: 14px;
}

/* Dropdown / spin box -- inset surface */
QComboBox, QSpinBox {
    background: #272d35;
    color: #eef1f4;
    border: 1px solid #353e48;
    border-radius: 8px;
    padding: 6px 10px;
    min-height: 20px;
}
QComboBox:hover, QSpinBox:hover { border-color: #46515d; }
QComboBox:focus, QSpinBox:focus { border-color: #19c3a3; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: #272d35; color: #eef1f4;
    selection-background-color: #19c3a3; selection-color: #06120f;
    border: 1px solid #353e48; border-radius: 8px; outline: none; padding: 4px;
}
QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; background: transparent; }

QCheckBox { color: #c4ccd4; spacing: 7px; }
QCheckBox::indicator {
    width: 16px; height: 16px; border-radius: 5px;
    border: 1px solid #46515d; background: #272d35;
}
QCheckBox::indicator:hover { border-color: #5a6571; }
QCheckBox::indicator:checked { background: #19c3a3; border-color: #19c3a3; }
QCheckBox:focus { outline: none; }

/* Segmented three-mode buttons (with vector icon + text) */
QPushButton[segmented="true"] {
    background: #272d35;
    color: #c4ccd4;
    border: 1px solid #353e48;
    border-radius: 9px;
    padding: 8px 14px;
    font-weight: 600;
    text-align: center;
}
QPushButton[segmented="true"]:hover { border-color: #46515d; color: #eef1f4; }
QPushButton[segmented="true"]:checked {
    background: #19c3a3; color: #06120f; border-color: #19c3a3;
}
QPushButton[segmented="true"]:focus { border-color: #19c3a3; }

/* Primary record button */
QPushButton#recordBtn {
    background: #19c3a3; color: #06120f;
    border: none; border-radius: 11px;
    padding: 13px 18px; font-size: 15px; font-weight: 700;
}
QPushButton#recordBtn:hover { background: #25d4b2; }
QPushButton#recordBtn:pressed { background: #14a98c; }
QPushButton#recordBtn:disabled { background: #2c343d; color: #6b7682; }

/* Small in-row card buttons (play/delete) */
QPushButton[rowbtn="true"] {
    background: transparent; border: none;
    border-radius: 8px; padding: 5px;
    min-width: 28px; min-height: 28px;
}
QPushButton[rowbtn="true"]:hover { background: #2c343d; }
QPushButton[rowbtn="true"]:focus { background: #2c343d; }

/* History list */
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { border: none; margin: 0 0 8px 0; padding: 0; }
QListWidget::item:selected { background: transparent; }

/* History card: tonal lift + hover border highlight */
QFrame#card {
    background: #1b2027;
    border: 1px solid #2b323b;
    border-radius: 11px;
}
QFrame#card:hover { background: #20262e; border-color: #3a434d; }
QLabel#cardTitle { color: #eef1f4; font-weight: 600; font-size: 13px; }
QLabel#cardMeta { color: #97a2ad; font-size: 12px; }

QToolTip {
    background: #272d35; color: #eef1f4;
    border: 1px solid #353e48; border-radius: 6px; padding: 4px 8px;
}

/* Persistent floating "Stop" bar shown while recording */
QFrame#stopHudPanel {
    background: #1b2027;
    border: 1px solid #2b323b;
    border-radius: 12px;
}
QPushButton#stopHudBtn {
    background: #e5484d; color: #ffffff;
    border: none; border-radius: 9px;
    padding: 8px 16px; font-size: 13px; font-weight: 700;
}
QPushButton#stopHudBtn:hover { background: #ef5a5f; }
QPushButton#stopHudBtn:pressed { background: #cc3b40; }
QLabel#stopHudHint { color: #97a2ad; font-size: 12px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #353e48; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #46515d; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)");
}

}
