#ifndef SNAPTRAY_DIALOG_THEME_UTILS_H
#define SNAPTRAY_DIALOG_THEME_UTILS_H

#include "settings/Settings.h"

#include <QColor>
#include <QString>

namespace SnapTray {
namespace DialogTheme {

struct Palette {
    QColor windowBackground;
    QColor titleBarBackground;
    QColor panelBackground;
    QColor border;
    QColor controlBorder;
    QColor textPrimary;
    QColor textSecondary;
    QColor inputBackground;
    QColor inputBorder;
    QColor buttonBackground;
    QColor buttonHoverBackground;
    QColor buttonPressedBackground;
    QColor buttonText;
    QColor buttonDisabledBackground;
    QColor buttonDisabledText;
    QColor buttonDisabledBorder;
    QColor selectionBackground;
    QColor selectionText;
    QColor accentBackground;
    QColor accentHoverBackground;
    QColor accentPressedBackground;
    QColor closeButtonHoverBackground;
    QColor successBackground;
    QColor successBorder;
    QColor successText;
};

inline bool isLightToolbarStyle()
{
    const auto settings = SnapTray::getSettings();
    const int styleValue = settings.value(QStringLiteral("appearance/toolbarStyle"), 0).toInt();
    return styleValue == 1;
}

inline Palette paletteForToolbarStyle()
{
    if (isLightToolbarStyle()) {
        return {
            QColor(248, 248, 248),  // windowBackground
            QColor(242, 242, 242),  // titleBarBackground
            QColor(236, 236, 236),  // panelBackground
            QColor(224, 224, 224),  // border
            QColor(208, 208, 208),  // controlBorder
            QColor(48, 48, 48),     // textPrimary
            QColor(120, 120, 120),  // textSecondary
            QColor(255, 255, 255),  // inputBackground
            QColor(208, 208, 208),  // inputBorder
            QColor(245, 245, 245),  // buttonBackground
            QColor(232, 232, 232),  // buttonHoverBackground
            QColor(220, 220, 220),  // buttonPressedBackground
            QColor(60, 60, 60),     // buttonText
            QColor(236, 236, 236),  // buttonDisabledBackground
            QColor(158, 158, 158),  // buttonDisabledText
            QColor(220, 220, 220),  // buttonDisabledBorder
            QColor(59, 130, 246, 72), // selectionBackground
            QColor(48, 48, 48),     // selectionText
            QColor(0x6C, 0x5C, 0xE7), // accentBackground
            QColor(0x5A, 0x4B, 0xD6), // accentHoverBackground
            QColor(0x4A, 0x3B, 0xC5), // accentPressedBackground
            QColor(220, 60, 60),    // closeButtonHoverBackground
            QColor(76, 175, 80),    // successBackground
            QColor(102, 187, 106),  // successBorder
            QColor(255, 255, 255)   // successText
        };
    }

    return {
        QColor(43, 43, 43),    // windowBackground
        QColor(43, 43, 43),    // titleBarBackground
        QColor(58, 58, 58),    // panelBackground
        QColor(61, 61, 61),    // border
        QColor(74, 74, 74),    // controlBorder
        QColor(224, 224, 224), // textPrimary
        QColor(158, 158, 158), // textSecondary
        QColor(30, 30, 30),    // inputBackground
        QColor(61, 61, 61),    // inputBorder
        QColor(58, 58, 58),    // buttonBackground
        QColor(74, 74, 74),    // buttonHoverBackground
        QColor(51, 51, 51),    // buttonPressedBackground
        QColor(224, 224, 224), // buttonText
        QColor(42, 42, 42),    // buttonDisabledBackground
        QColor(102, 102, 102), // buttonDisabledText
        QColor(58, 58, 58),    // buttonDisabledBorder
        QColor(96, 165, 250, 112), // selectionBackground
        QColor(224, 224, 224), // selectionText
        QColor(0xA2, 0x9B, 0xFE), // accentBackground
        QColor(0x6C, 0x5C, 0xE7), // accentHoverBackground
        QColor(0x5A, 0x4B, 0xD6), // accentPressedBackground
        QColor(200, 60, 60),   // closeButtonHoverBackground
        QColor(76, 175, 80),   // successBackground
        QColor(102, 187, 106), // successBorder
        QColor(255, 255, 255)  // successText
    };
}

// Shared CSS generators — implemented in DialogThemeUtils.cpp
QString baseStylesheet(const Palette& palette);
QString successButtonStylesheet(const Palette& palette);

inline QString toCssColor(const QColor& color)
{
    if (color.alpha() == 255) {
        return color.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

}  // namespace DialogTheme
}  // namespace SnapTray

#endif  // SNAPTRAY_DIALOG_THEME_UTILS_H
