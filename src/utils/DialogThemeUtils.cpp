#include "utils/DialogThemeUtils.h"

namespace SnapTray {
namespace DialogTheme {

QString baseStylesheet(const Palette& palette)
{
    return QStringLiteral(R"(
        #dialogRoot {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 10px;
        }

        #titleBar {
            background-color: %3;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            border-bottom: 1px solid %2;
        }

        #iconLabel, #thumbnail {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            color: %6;
            font-size: 18px;
            font-weight: bold;
        }

        #titleLabel, #formatLabel {
            color: %7;
            font-size: 14px;
            font-weight: bold;
        }

        #charCountLabel, #characterCountLabel, #subtitleLabel {
            color: %6;
            font-size: 12px;
        }

        QTextEdit, QLineEdit {
            background-color: %8;
            color: %7;
            border: 1px solid %9;
            border-radius: 6px;
            padding: 8px;
            font-size: 13px;
            selection-background-color: %10;
        }

        #buttonBar {
            background-color: %3;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }

        QPushButton {
            background-color: %11;
            color: %12;
            border: 1px solid %5;
            border-radius: 6px;
            font-size: 13px;
            font-weight: 500;
            padding: 8px 16px;
        }

        QPushButton:hover {
            background-color: %13;
            border-color: %2;
        }

        QPushButton:pressed {
            background-color: %14;
        }

        QPushButton:disabled {
            background-color: %15;
            color: %16;
            border-color: %17;
        }
    )")
    .arg(toCssColor(palette.windowBackground))       // %1
    .arg(toCssColor(palette.border))                  // %2
    .arg(toCssColor(palette.titleBarBackground))      // %3
    .arg(toCssColor(palette.panelBackground))         // %4
    .arg(toCssColor(palette.controlBorder))           // %5
    .arg(toCssColor(palette.textSecondary))           // %6
    .arg(toCssColor(palette.textPrimary))             // %7
    .arg(toCssColor(palette.inputBackground))         // %8
    .arg(toCssColor(palette.inputBorder))             // %9
    .arg(toCssColor(palette.selectionBackground))     // %10
    .arg(toCssColor(palette.buttonBackground))        // %11
    .arg(toCssColor(palette.buttonText))              // %12
    .arg(toCssColor(palette.buttonHoverBackground))   // %13
    .arg(toCssColor(palette.buttonPressedBackground)) // %14
    .arg(toCssColor(palette.buttonDisabledBackground))// %15
    .arg(toCssColor(palette.buttonDisabledText))      // %16
    .arg(toCssColor(palette.buttonDisabledBorder));   // %17
}

QString successButtonStylesheet(const Palette& palette)
{
    return QStringLiteral(
        "QPushButton { background-color: %1; border-color: %2; color: %3; }")
        .arg(toCssColor(palette.successBackground))
        .arg(toCssColor(palette.successBorder))
        .arg(toCssColor(palette.successText));
}

}  // namespace DialogTheme
}  // namespace SnapTray
