#include "ui/DesignTokens.h"

namespace SnapTray {

static const DesignTokens s_darkTokens = {
    // Status accents (vivid Tailwind colors for dark backgrounds)
    QColor(52, 211, 153),     // successAccent  — emerald-400
    QColor(248, 113, 113),    // errorAccent    — red-400
    QColor(251, 191, 36),     // warningAccent  — amber-400
    QColor(96, 165, 250),     // infoAccent     — blue-400

    // Toast surface (dark glass) — aligned with QML ComponentTokens
    QColor::fromRgbF(0.12, 0.12, 0.12, 0.90),  // toastBackground
    QColor(255, 255, 255, 15),                   // toastHighlight
    QColor::fromRgbF(1, 1, 1, 0.10),            // toastBorder
    QColor(245, 245, 245),                       // toastTitleColor  — gray100
    QColor(189, 189, 189),                       // toastMessageColor — gray400

    // Badge surface — aligned with QML ComponentTokens
    QColor::fromRgbF(0, 0, 0, 0.71),            // badgeBackground
    QColor(255, 255, 255),                       // badgeText
};

static const DesignTokens s_lightTokens = {
    // Status accents (slightly deeper for light backgrounds)
    QColor(16, 185, 129),     // successAccent  — emerald-500
    QColor(239, 68, 68),      // errorAccent    — red-500
    QColor(245, 158, 11),     // warningAccent  — amber-500
    QColor(59, 130, 246),     // infoAccent     — blue-500

    // Toast surface (light glass) — aligned with QML ComponentTokens
    QColor::fromRgbF(1, 1, 1, 0.92),            // toastBackground
    QColor(255, 255, 255, 200),                  // toastHighlight
    QColor::fromRgbF(0, 0, 0, 0.06),            // toastBorder
    QColor(33, 33, 33),                          // toastTitleColor  — gray900
    QColor(117, 117, 117),                       // toastMessageColor — gray600

    // Badge surface — aligned with QML ComponentTokens
    QColor::fromRgbF(1, 1, 1, 0.78),            // badgeBackground
    QColor::fromRgbF(0.16, 0.16, 0.16, 1.0),    // badgeText
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
