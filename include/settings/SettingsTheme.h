/**
 * @file SettingsTheme.h
 * @brief Centralized theme-aware colors for Settings dialog components
 */

#pragma once

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QString>
#include <QWidget>
#include "ToolbarStyle.h"
#include "ui/DesignTokens.h"

namespace SnapTray {
namespace SettingsTheme {

/**
 * @brief Detects if the current theme is dark mode (matches ThemeManager logic)
 */
inline bool isDark() {
    return ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark;
}

// ============================================================================
// Borders and backgrounds
// ============================================================================

inline QString borderColor() { return isDark() ? QStringLiteral("#404040") : QStringLiteral("#E0E0E0"); }
inline QString panelBackground() { return isDark() ? QStringLiteral("#2E2E50") : QStringLiteral("#FAFAFA"); }
inline QString hoverBackground() { return isDark() ? QStringLiteral("#3A3A5E") : QStringLiteral("#F5F5F5"); }
inline QString dialogBackground() { return isDark() ? QStringLiteral("#2E2E50") : QStringLiteral("#FFFFFF"); }

// ============================================================================
// Info/warning panels
// ============================================================================

inline QString infoPanelBg() { return isDark() ? QStringLiteral("#1E3A5F") : QStringLiteral("#E3F2FD"); }
inline QString infoPanelText() { return isDark() ? QStringLiteral("#64B5F6") : QStringLiteral("#1565C0"); }
inline QString warningPanelBg() { return isDark() ? QStringLiteral("#5D4037") : QStringLiteral("#FFF3CD"); }
inline QString warningPanelText() { return isDark() ? QStringLiteral("#FFCC80") : QStringLiteral("#856404"); }

// ============================================================================
// Text colors
// ============================================================================

inline QString primaryText() { return isDark() ? QStringLiteral("#E0E0E0") : QStringLiteral("#212121"); }
inline QString secondaryText() { return isDark() ? QStringLiteral("#909090") : QStringLiteral("#757575"); }
inline QString mutedText() { return isDark() ? QStringLiteral("#707070") : QStringLiteral("#9E9E9E"); }

// ============================================================================
// Links
// ============================================================================

inline QString linkColor() { return isDark() ? QStringLiteral("#64B5F6") : QStringLiteral("#0066CC"); }

// ============================================================================
// Status colors (vivid colors work well in both themes)
// ============================================================================

inline QString successColor() {
    auto type = isDark() ? ToolbarStyleType::Dark : ToolbarStyleType::Light;
    return DesignTokens::forStyle(type).successAccent.name();
}
inline QString errorColor() {
    auto type = isDark() ? ToolbarStyleType::Dark : ToolbarStyleType::Light;
    return DesignTokens::forStyle(type).errorAccent.name();
}

// ============================================================================
// Selection
// ============================================================================

inline QString selectionBg() { return isDark() ? QStringLiteral("#1E3A5F") : QStringLiteral("#E3F2FD"); }
inline QString selectionText() { return isDark() ? QStringLiteral("#64B5F6") : QStringLiteral("#1976D2"); }

// ============================================================================
// Buttons
// ============================================================================

inline QString buttonBg() { return isDark() ? QStringLiteral("#424242") : QStringLiteral("#E0E0E0"); }
inline QString buttonHoverBg() { return isDark() ? QStringLiteral("#4A4A4A") : QStringLiteral("#D0D0D0"); }
inline QString buttonPressedBg() { return isDark() ? QStringLiteral("#383838") : QStringLiteral("#C0C0C0"); }
inline QString buttonText() { return isDark() ? QStringLiteral("#E0E0E0") : QStringLiteral("#333333"); }
inline QString primaryButtonBg() { return QStringLiteral("#6C5CE7"); }
inline QString primaryButtonHoverBg() { return QStringLiteral("#5A4BD6"); }
inline QString primaryButtonPressedBg() { return QStringLiteral("#4A3BC5"); }
inline QString disabledButtonBg() { return isDark() ? QStringLiteral("#383838") : QStringLiteral("#B0BEC5"); }
inline QString disabledButtonText() { return isDark() ? QStringLiteral("#606060") : QStringLiteral("#78909C"); }

// ============================================================================
// Input fields
// ============================================================================

inline QString inputFocusBorder() { return isDark() ? QStringLiteral("#64B5F6") : QStringLiteral("#90CAF9"); }
inline QString inputFocusBg() { return isDark() ? QStringLiteral("#1E3A5F") : QStringLiteral("#E3F2FD"); }

// ============================================================================
// Widget helpers
// ============================================================================

inline void applyHeaderFont(QWidget* label) {
    QFont f = label->font();
    f.setBold(true);
    f.setPixelSize(12);
    label->setFont(f);
}

inline void applySecondaryColor(QWidget* widget) {
    QPalette p = widget->palette();
    p.setColor(QPalette::WindowText, QColor(secondaryText()));
    widget->setPalette(p);
}

}  // namespace SettingsTheme
}  // namespace SnapTray
