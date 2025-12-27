#ifndef TOOLBARSTYLE_H
#define TOOLBARSTYLE_H

#include <QColor>
#include <QString>

/**
 * @brief Toolbar style type enumeration.
 */
enum class ToolbarStyleType {
    Dark = 0,   // Default dark style
    Light = 1   // PixPin/Snipaste light style
};

/**
 * @brief Toolbar style configuration containing all colors and settings.
 */
struct ToolbarStyleConfig {
    // Background colors
    QColor backgroundColorTop;
    QColor backgroundColorBottom;
    QColor borderColor;
    int shadowAlpha;

    // Icon colors
    QColor iconNormalColor;
    QColor iconActiveColor;
    QColor iconHoveredColor;
    QColor iconActionColor;     // Pin, Save, Copy buttons
    QColor iconCancelColor;     // Cancel/Close button
    QColor iconRecordColor;     // Record button

    // State indicators
    QColor activeBackgroundColor;   // Dark style uses background highlight
    QColor hoverBackgroundColor;
    bool useRedDotIndicator;        // Light style uses red dot indicator
    QColor redDotColor;
    int redDotSize;

    // Separators and tooltips
    QColor separatorColor;
    QColor tooltipBackground;
    QColor tooltipBorder;
    QColor tooltipText;

    // Secondary UI elements (ColorAndWidthWidget, dropdowns, etc.)
    QColor buttonActiveColor;       // Active button background in sub-widgets
    QColor buttonHoverColor;        // Hover button background in sub-widgets
    QColor buttonInactiveColor;     // Inactive button background
    QColor textColor;               // Text color for labels
    QColor textActiveColor;         // Text color when active
    QColor dropdownBackground;      // Dropdown menu background
    QColor dropdownBorder;          // Dropdown menu border

    // Glass effect properties (macOS style)
    QColor glassBackgroundColor;    // Semi-transparent base color
    QColor glassHighlightColor;     // Top edge inner glow
    QColor hairlineBorderColor;     // Low opacity hairline border

    // Enhanced shadow
    QColor shadowColor;             // Shadow color with alpha
    int shadowBlurRadius;           // Shadow spread/blur radius
    int shadowOffsetY;              // Vertical shadow offset

    // Corner radius
    int cornerRadius;               // Default corner radius (10 for toolbar, 6 for sub-widgets)

    /**
     * @brief Get the dark style configuration.
     */
    static ToolbarStyleConfig getDarkStyle();

    /**
     * @brief Get the light style configuration.
     */
    static ToolbarStyleConfig getLightStyle();

    /**
     * @brief Get style configuration by type.
     */
    static ToolbarStyleConfig getStyle(ToolbarStyleType type);

    /**
     * @brief Load the saved style from QSettings.
     */
    static ToolbarStyleType loadStyle();

    /**
     * @brief Save the style to QSettings.
     */
    static void saveStyle(ToolbarStyleType type);

    /**
     * @brief Get the display name for a style type.
     */
    static QString styleTypeName(ToolbarStyleType type);
};

#endif // TOOLBARSTYLE_H
