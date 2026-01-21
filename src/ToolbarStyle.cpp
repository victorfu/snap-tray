#include "ToolbarStyle.h"
#include "settings/Settings.h"

#include <QSettings>

const ToolbarStyleConfig& ToolbarStyleConfig::getDarkStyle()
{
    static const ToolbarStyleConfig config = {
        // Background - dark gradient
        .backgroundColorTop = QColor(55, 55, 55, 245),
        .backgroundColorBottom = QColor(40, 40, 40, 245),
        .borderColor = QColor(70, 70, 70),
        .shadowAlpha = 50,

        // Icon colors
        .iconNormalColor = QColor(220, 220, 220),
        .iconActiveColor = Qt::white,
        .iconHoveredColor = QColor(220, 220, 220),
        .iconActionColor = QColor(100, 200, 255),     // Blue for Pin, Save, Copy
        .iconCancelColor = QColor(255, 100, 100),     // Red for Cancel
        .iconRecordColor = QColor(255, 80, 80),       // Red for Record

        // State indicators - use background highlight for dark style
        .activeBackgroundColor = QColor(0, 120, 200),
        .hoverBackgroundColor = QColor(80, 80, 80),
        .useRedDotIndicator = false,
        .redDotColor = QColor(255, 68, 68),
        .redDotSize = 6,

        // Separators and tooltips
        .separatorColor = QColor(80, 80, 80),
        .tooltipBackground = QColor(30, 30, 30, 230),
        .tooltipBorder = QColor(80, 80, 80),
        .tooltipText = Qt::white,

        // Secondary UI elements
        .buttonActiveColor = QColor(0, 122, 255),
        .buttonHoverColor = QColor(80, 80, 80),
        .buttonInactiveColor = QColor(50, 50, 50),
        .textColor = QColor(200, 200, 200),
        .textActiveColor = Qt::white,
        .dropdownBackground = QColor(50, 50, 50, 250),
        .dropdownBorder = QColor(70, 70, 70),

        // Glass effect properties (macOS dark style)
        .glassBackgroundColor = QColor(40, 40, 40, 217),   // 85% opacity
        .glassHighlightColor = QColor(255, 255, 255, 15),  // Subtle top edge glow
        .hairlineBorderColor = QColor(255, 255, 255, 25),  // 10% white hairline

        // Enhanced shadow
        .shadowColor = QColor(0, 0, 0, 60),
        .shadowBlurRadius = 12,
        .shadowOffsetY = 4,

        // Corner radius
        .cornerRadius = 10,
    };
    return config;
}

const ToolbarStyleConfig& ToolbarStyleConfig::getLightStyle()
{
    static const ToolbarStyleConfig config = {
        // Background - light gradient
        .backgroundColorTop = QColor(248, 248, 248, 245),
        .backgroundColorBottom = QColor(240, 240, 240, 245),
        .borderColor = QColor(224, 224, 224),
        .shadowAlpha = 30,

        // Icon colors - optimized for light background
        .iconNormalColor = QColor(60, 60, 60),      // Darker for better visibility
        .iconActiveColor = Qt::white,                // White for contrast on blue highlight
        .iconHoveredColor = QColor(70, 70, 70),     // Darker on hover for feedback
        .iconActionColor = QColor(0, 122, 255),     // Blue for Pin, Save, Copy
        .iconCancelColor = QColor(220, 60, 60),     // Red for Cancel
        .iconRecordColor = QColor(220, 50, 50),     // Red for Record

        // State indicators - use background highlight (similar to dark style but lighter blue)
        .activeBackgroundColor = QColor(0, 140, 220),    // Slightly lighter blue than dark style
        .hoverBackgroundColor = QColor(220, 220, 220),
        .useRedDotIndicator = false,
        .redDotColor = QColor(255, 68, 68),
        .redDotSize = 6,

        // Separators and tooltips
        .separatorColor = QColor(208, 208, 208),
        .tooltipBackground = QColor(255, 255, 255, 240),
        .tooltipBorder = QColor(200, 200, 200),
        .tooltipText = QColor(50, 50, 50),

        // Secondary UI elements
        .buttonActiveColor = QColor(0, 122, 255),
        .buttonHoverColor = QColor(232, 232, 232),
        .buttonInactiveColor = QColor(245, 245, 245),
        .textColor = QColor(80, 80, 80),
        .textActiveColor = Qt::white,
        .dropdownBackground = QColor(255, 255, 255, 250),
        .dropdownBorder = QColor(200, 200, 200),

        // Glass effect properties (macOS light style)
        .glassBackgroundColor = QColor(255, 255, 255, 230),  // 90% opacity
        .glassHighlightColor = QColor(255, 255, 255, 200),   // Bright top edge
        .hairlineBorderColor = QColor(0, 0, 0, 20),          // 8% black hairline

        // Enhanced shadow
        .shadowColor = QColor(0, 0, 0, 30),
        .shadowBlurRadius = 8,
        .shadowOffsetY = 2,

        // Corner radius
        .cornerRadius = 10,
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
