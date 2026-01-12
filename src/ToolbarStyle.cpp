#include "ToolbarStyle.h"
#include "settings/Settings.h"

#include <QSettings>

ToolbarStyleConfig ToolbarStyleConfig::getDarkStyle()
{
    ToolbarStyleConfig config;

    // Background - dark gradient
    config.backgroundColorTop = QColor(55, 55, 55, 245);
    config.backgroundColorBottom = QColor(40, 40, 40, 245);
    config.borderColor = QColor(70, 70, 70);
    config.shadowAlpha = 50;

    // Icon colors
    config.iconNormalColor = QColor(220, 220, 220);
    config.iconActiveColor = Qt::white;
    config.iconHoveredColor = QColor(220, 220, 220);
    config.iconActionColor = QColor(100, 200, 255);     // Blue for Pin, Save, Copy
    config.iconCancelColor = QColor(255, 100, 100);     // Red for Cancel
    config.iconRecordColor = QColor(255, 80, 80);       // Red for Record

    // State indicators - use background highlight for dark style
    config.activeBackgroundColor = QColor(0, 120, 200);
    config.hoverBackgroundColor = QColor(80, 80, 80);
    config.useRedDotIndicator = false;
    config.redDotColor = QColor(255, 68, 68);
    config.redDotSize = 6;

    // Separators and tooltips
    config.separatorColor = QColor(80, 80, 80);
    config.tooltipBackground = QColor(30, 30, 30, 230);
    config.tooltipBorder = QColor(80, 80, 80);
    config.tooltipText = Qt::white;

    // Secondary UI elements
    config.buttonActiveColor = QColor(0, 122, 255);
    config.buttonHoverColor = QColor(80, 80, 80);
    config.buttonInactiveColor = QColor(50, 50, 50);
    config.textColor = QColor(200, 200, 200);
    config.textActiveColor = Qt::white;
    config.dropdownBackground = QColor(50, 50, 50, 250);
    config.dropdownBorder = QColor(70, 70, 70);

    // Glass effect properties (macOS dark style)
    config.glassBackgroundColor = QColor(40, 40, 40, 217);   // 85% opacity
    config.glassHighlightColor = QColor(255, 255, 255, 15);  // Subtle top edge glow
    config.hairlineBorderColor = QColor(255, 255, 255, 25);  // 10% white hairline

    // Enhanced shadow
    config.shadowColor = QColor(0, 0, 0, 60);
    config.shadowBlurRadius = 12;
    config.shadowOffsetY = 4;

    // Corner radius
    config.cornerRadius = 10;

    return config;
}

ToolbarStyleConfig ToolbarStyleConfig::getLightStyle()
{
    ToolbarStyleConfig config;

    // Background - light gradient
    config.backgroundColorTop = QColor(248, 248, 248, 245);
    config.backgroundColorBottom = QColor(240, 240, 240, 245);
    config.borderColor = QColor(224, 224, 224);
    config.shadowAlpha = 30;

    // Icon colors - optimized for light background
    config.iconNormalColor = QColor(60, 60, 60);    // Darker for better visibility
    config.iconActiveColor = Qt::white;              // White for contrast on blue highlight
    config.iconHoveredColor = QColor(70, 70, 70);   // Darker on hover for feedback
    config.iconActionColor = QColor(0, 122, 255);       // Blue for Pin, Save, Copy
    config.iconCancelColor = QColor(220, 60, 60);       // Red for Cancel
    config.iconRecordColor = QColor(220, 50, 50);       // Red for Record

    // State indicators - use background highlight (similar to dark style but lighter blue)
    config.activeBackgroundColor = QColor(0, 140, 220);    // Slightly lighter blue than dark style
    config.hoverBackgroundColor = QColor(220, 220, 220);
    config.useRedDotIndicator = false;
    config.redDotColor = QColor(255, 68, 68);
    config.redDotSize = 6;

    // Separators and tooltips
    config.separatorColor = QColor(208, 208, 208);
    config.tooltipBackground = QColor(255, 255, 255, 240);
    config.tooltipBorder = QColor(200, 200, 200);
    config.tooltipText = QColor(50, 50, 50);

    // Secondary UI elements
    config.buttonActiveColor = QColor(0, 122, 255);
    config.buttonHoverColor = QColor(232, 232, 232);
    config.buttonInactiveColor = QColor(245, 245, 245);
    config.textColor = QColor(80, 80, 80);
    config.textActiveColor = Qt::white;
    config.dropdownBackground = QColor(255, 255, 255, 250);
    config.dropdownBorder = QColor(200, 200, 200);

    // Glass effect properties (macOS light style)
    config.glassBackgroundColor = QColor(255, 255, 255, 230);  // 90% opacity
    config.glassHighlightColor = QColor(255, 255, 255, 200);   // Bright top edge
    config.hairlineBorderColor = QColor(0, 0, 0, 20);          // 8% black hairline

    // Enhanced shadow
    config.shadowColor = QColor(0, 0, 0, 30);
    config.shadowBlurRadius = 8;
    config.shadowOffsetY = 2;

    // Corner radius
    config.cornerRadius = 10;

    return config;
}

ToolbarStyleConfig ToolbarStyleConfig::getStyle(ToolbarStyleType type)
{
    switch (type) {
    case ToolbarStyleType::Light:
        return getLightStyle();
    case ToolbarStyleType::Dark:
    default:
        return getDarkStyle();
    }
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
