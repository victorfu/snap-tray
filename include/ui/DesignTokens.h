#ifndef DESIGNTOKENS_H
#define DESIGNTOKENS_H

#include <QColor>
#include "ToolbarStyle.h"

namespace SnapTray {

/**
 * @brief Semantic color tokens for the SnapTray UI.
 *
 * Provides consistent colors across all UI components (toasts, badges, overlays).
 * Keyed to ToolbarStyleType (Dark/Light), which is the user's chosen theme.
 *
 * This is a thin data layer on top of ToolbarStyleConfig — it does NOT replace it.
 * ToolbarStyleConfig continues to own toolbar-specific colors (icon states, gradients, etc.).
 * DesignTokens owns semantic colors for notifications and overlays.
 */
struct DesignTokens {
    // ---- Status accent colors (left border strip in toasts, icon tint) ----
    QColor successAccent;
    QColor errorAccent;
    QColor warningAccent;
    QColor infoAccent;

    // ---- Toast / overlay surface ----
    QColor toastBackground;
    QColor toastHighlight;
    QColor toastBorder;
    QColor toastTitleColor;
    QColor toastMessageColor;

    // ---- Badge surface (zoom/opacity indicators) ----
    QColor badgeBackground;
    QColor badgeText;

    /**
     * @brief Get tokens for a specific toolbar style.
     */
    static const DesignTokens& forStyle(ToolbarStyleType type);

    /**
     * @brief Get tokens for the currently active toolbar style.
     * Reads ToolbarStyleConfig::loadStyle() each call.
     */
    static const DesignTokens& current();
};

} // namespace SnapTray

#endif // DESIGNTOKENS_H
