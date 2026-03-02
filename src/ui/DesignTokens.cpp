#include "ui/DesignTokens.h"

namespace SnapTray {

static const DesignTokens s_darkTokens = {
    // Status accents (vivid Tailwind colors for dark backgrounds)
    QColor(52, 211, 153),     // successAccent  — emerald-400
    QColor(248, 113, 113),    // errorAccent    — red-400
    QColor(251, 191, 36),     // warningAccent  — amber-400
    QColor(96, 165, 250),     // infoAccent     — blue-400

    // Toast surface (dark glass)
    QColor(30, 30, 30, 230),     // toastBackground
    QColor(255, 255, 255, 15),   // toastHighlight
    QColor(255, 255, 255, 25),   // toastBorder
    QColor(240, 240, 240),       // toastTitleColor
    QColor(200, 200, 200),       // toastMessageColor

    // Badge surface
    QColor(0, 0, 0, 180),        // badgeBackground
    QColor(255, 255, 255),        // badgeText
};

static const DesignTokens s_lightTokens = {
    // Status accents (slightly deeper for light backgrounds)
    QColor(16, 185, 129),     // successAccent  — emerald-500
    QColor(239, 68, 68),      // errorAccent    — red-500
    QColor(245, 158, 11),     // warningAccent  — amber-500
    QColor(59, 130, 246),     // infoAccent     — blue-500

    // Toast surface (light glass)
    QColor(255, 255, 255, 235),  // toastBackground
    QColor(255, 255, 255, 200),  // toastHighlight
    QColor(0, 0, 0, 20),         // toastBorder
    QColor(30, 30, 30),          // toastTitleColor
    QColor(80, 80, 80),          // toastMessageColor

    // Badge surface
    QColor(255, 255, 255, 200),  // badgeBackground
    QColor(40, 40, 40),          // badgeText
};

const DesignTokens& DesignTokens::forStyle(ToolbarStyleType type)
{
    return (type == ToolbarStyleType::Light) ? s_lightTokens : s_darkTokens;
}

const DesignTokens& DesignTokens::current()
{
    return forStyle(ToolbarStyleConfig::loadStyle());
}

} // namespace SnapTray
