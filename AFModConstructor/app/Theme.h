#pragma once

#include <QApplication>
#include <QColor>
#include <QFont>

namespace Theme {

    struct Palette {
        QColor windowBg    = QColor(0x16, 0x18, 0x1f);
        QColor panelBg     = QColor(0x1c, 0x1f, 0x28);
        QColor cardBg      = QColor(0x21, 0x25, 0x30);
        QColor cardHover   = QColor(0x28, 0x2d, 0x3b);
        QColor cardSelect  = QColor(0x2e, 0x2a, 0x45);
        QColor groupBg     = QColor(0x24, 0x28, 0x36);
        QColor border      = QColor(0x31, 0x36, 0x47);
        QColor borderSoft  = QColor(0x27, 0x2c, 0x3a);
        QColor text        = QColor(0xe6, 0xe8, 0xf0);
        QColor textMuted   = QColor(0x8c, 0x93, 0xa8);
        QColor textNumber  = QColor(0x6b, 0x73, 0x8f);
        QColor accent      = QColor(0x9b, 0x87, 0xf5);
        QColor action      = QColor(0x6f, 0xd4, 0x9b);
        QColor danger      = QColor(0xe5, 0x67, 0x67);
        QColor warning     = QColor(0xe5, 0xb8, 0x5c);
        QColor pillBg      = QColor(0x2c, 0x32, 0x44);
        QColor pillBorder  = QColor(0x46, 0x4f, 0x6b);
        QColor pillHover   = QColor(0x39, 0x41, 0x59);
        QColor pillText    = QColor(0xd8, 0xde, 0xf2);
        QColor exprText    = QColor(0x7e, 0xc8, 0xf8);
    };

    const Palette& Colors();
    void Apply(QApplication& app);
    QFont MonoFont(int pointSize = 9);

} // namespace Theme
