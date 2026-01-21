#include "ToolbarStyle.h"
#include "settings/Settings.h"

#include <QSettings>

const ToolbarStyleConfig& ToolbarStyleConfig::getDarkStyle()
{
    static const ToolbarStyleConfig config = {
        // Background - dark gradient
        QColor(55, 55, 55, 245),   // backgroundColorTop
        QColor(40, 40, 40, 245),   // backgroundColorBottom
        QColor(70, 70, 70),        // borderColor
        50,                        // shadowAlpha

        // Icon colors
        QColor(220, 220, 220),     // iconNormalColor
        Qt::white,                 // iconActiveColor
        QColor(220, 220, 220),     // iconHoveredColor
        QColor(100, 200, 255),     // iconActionColor - Blue for Pin, Save, Copy
        QColor(255, 100, 100),     // iconCancelColor - Red for Cancel
        QColor(255, 80, 80),       // iconRecordColor - Red for Record

        // State indicators - use background highlight for dark style
        QColor(0, 120, 200),       // activeBackgroundColor
        QColor(80, 80, 80),        // hoverBackgroundColor
        false,                     // useRedDotIndicator
        QColor(255, 68, 68),       // redDotColor
        6,                         // redDotSize

        // Separators and tooltips
        QColor(80, 80, 80),        // separatorColor
        QColor(30, 30, 30, 230),   // tooltipBackground
        QColor(80, 80, 80),        // tooltipBorder
        Qt::white,                 // tooltipText

        // Secondary UI elements
        QColor(0, 122, 255),       // buttonActiveColor
        QColor(80, 80, 80),        // buttonHoverColor
        QColor(50, 50, 50),        // buttonInactiveColor
        QColor(200, 200, 200),     // textColor
        Qt::white,                 // textActiveColor
        QColor(50, 50, 50, 250),   // dropdownBackground
        QColor(70, 70, 70),        // dropdownBorder

        // Glass effect properties (macOS dark style)
        QColor(40, 40, 40, 217),   // glassBackgroundColor - 85% opacity
        QColor(255, 255, 255, 15), // glassHighlightColor - Subtle top edge glow
        QColor(255, 255, 255, 25), // hairlineBorderColor - 10% white hairline

        // Enhanced shadow
        QColor(0, 0, 0, 60),       // shadowColor
        12,                        // shadowBlurRadius
        4,                         // shadowOffsetY

        // Corner radius
        10                         // cornerRadius
    };
    return config;
}

const ToolbarStyleConfig& ToolbarStyleConfig::getLightStyle()
{
    static const ToolbarStyleConfig config = {
        // Background - light gradient
        QColor(248, 248, 248, 245), // backgroundColorTop
        QColor(240, 240, 240, 245), // backgroundColorBottom
        QColor(224, 224, 224),      // borderColor
        30,                         // shadowAlpha

        // Icon colors - optimized for light background
        QColor(60, 60, 60),         // iconNormalColor - Darker for better visibility
        Qt::white,                  // iconActiveColor - White for contrast on blue highlight
        QColor(70, 70, 70),         // iconHoveredColor - Darker on hover for feedback
        QColor(0, 122, 255),        // iconActionColor - Blue for Pin, Save, Copy
        QColor(220, 60, 60),        // iconCancelColor - Red for Cancel
        QColor(220, 50, 50),        // iconRecordColor - Red for Record

        // State indicators - use background highlight (similar to dark style but lighter blue)
        QColor(0, 140, 220),        // activeBackgroundColor - Slightly lighter blue than dark style
        QColor(220, 220, 220),      // hoverBackgroundColor
        false,                      // useRedDotIndicator
        QColor(255, 68, 68),        // redDotColor
        6,                          // redDotSize

        // Separators and tooltips
        QColor(208, 208, 208),      // separatorColor
        QColor(255, 255, 255, 240), // tooltipBackground
        QColor(200, 200, 200),      // tooltipBorder
        QColor(50, 50, 50),         // tooltipText

        // Secondary UI elements
        QColor(0, 122, 255),        // buttonActiveColor
        QColor(232, 232, 232),      // buttonHoverColor
        QColor(245, 245, 245),      // buttonInactiveColor
        QColor(80, 80, 80),         // textColor
        Qt::white,                  // textActiveColor
        QColor(255, 255, 255, 250), // dropdownBackground
        QColor(200, 200, 200),      // dropdownBorder

        // Glass effect properties (macOS light style)
        QColor(255, 255, 255, 230), // glassBackgroundColor - 90% opacity
        QColor(255, 255, 255, 200), // glassHighlightColor - Bright top edge
        QColor(0, 0, 0, 20),        // hairlineBorderColor - 8% black hairline

        // Enhanced shadow
        QColor(0, 0, 0, 30),        // shadowColor
        8,                          // shadowBlurRadius
        2,                          // shadowOffsetY

        // Corner radius
        10                          // cornerRadius
    };
    return config;
}

const ToolbarStyleConfig& ToolbarStyleConfig::getStyle(ToolbarStyleType type)
{
    return (type == ToolbarStyleType::Light) ? getLightStyle() : getDarkStyle();
}

ToolbarStyleType ToolbarStyleConfig::loadStyle()
{
    auto settings = SnapTray::getSettings();
    int styleValue = settings.value("appearance/toolbarStyle", 0).toInt();
    return static_cast<ToolbarStyleType>(styleValue);
}

void ToolbarStyleConfig::saveStyle(ToolbarStyleType type)
{
    auto settings = SnapTray::getSettings();
    settings.setValue("appearance/toolbarStyle", static_cast<int>(type));
}

QString ToolbarStyleConfig::styleTypeName(ToolbarStyleType type)
{
    switch (type) {
    case ToolbarStyleType::Light:
        return QObject::tr("Light");
    case ToolbarStyleType::Dark:
    default:
        return QObject::tr("Dark");
    }
}
