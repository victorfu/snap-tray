/**
 * @file SettingsTheme.h
 * @brief Centralized theme-aware colors for Settings dialog components
 */

#pragma once

#include <QApplication>
#include <QPalette>
#include <QString>

namespace SnapTray {
namespace SettingsTheme {

/**
 * @brief Detects if the current system theme is dark mode
 */
inline bool isDark() {
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

// ============================================================================
// Borders and backgrounds
// ============================================================================

inline QString borderColor() { return isDark() ? QStringLiteral("#404040") : QStringLiteral("#E0E0E0"); }
inline QString panelBackground() { return isDark() ? QStringLiteral("#2D2D2D") : QStringLiteral("#FAFAFA"); }
inline QString hoverBackground() { return isDark() ? QStringLiteral("#3A3A3A") : QStringLiteral("#F5F5F5"); }
inline QString dialogBackground() { return isDark() ? QStringLiteral("#2D2D2D") : QStringLiteral("#FFFFFF"); }

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

inline QString successColor() { return QStringLiteral("#4CAF50"); }
inline QString errorColor() { return QStringLiteral("#F44336"); }

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
inline QString primaryButtonBg() { return QStringLiteral("#1976D2"); }
inline QString primaryButtonHoverBg() { return QStringLiteral("#1565C0"); }
inline QString primaryButtonPressedBg() { return QStringLiteral("#0D47A1"); }
inline QString disabledButtonBg() { return isDark() ? QStringLiteral("#383838") : QStringLiteral("#B0BEC5"); }
inline QString disabledButtonText() { return isDark() ? QStringLiteral("#606060") : QStringLiteral("#78909C"); }

// ============================================================================
// Input fields
// ============================================================================

inline QString inputFocusBorder() { return isDark() ? QStringLiteral("#64B5F6") : QStringLiteral("#90CAF9"); }
inline QString inputFocusBg() { return isDark() ? QStringLiteral("#1E3A5F") : QStringLiteral("#E3F2FD"); }

}  // namespace SettingsTheme
}  // namespace SnapTray
