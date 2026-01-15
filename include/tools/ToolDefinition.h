#ifndef TOOLDEFINITION_H
#define TOOLDEFINITION_H

#include <QString>
#include <QColor>
#include "ToolId.h"

/**
 * @brief Tool category classification.
 */
enum class ToolCategory {
    Selection,  // Selection/pointer tools
    Drawing,    // Creates annotations
    Toggle,     // Toggle on/off behavior
    Action      // One-shot actions
};

/**
 * @brief Metadata structure for tool definitions.
 *
 * Contains all static information about a tool including
 * its icon, tooltip, category, and capabilities.
 */
struct ToolDefinition {
    ToolId id;
    QString iconKey;
    QString tooltip;
    QString shortcut;
    ToolCategory category;

    // Tool capabilities
    bool hasColor = false;
    bool hasWidth = false;
    bool hasTextFormatting = false;
    bool hasArrowStyle = false;
    bool hasShapeType = false;
    bool hasFillMode = false;

    // UI visibility for sub-toolbar
    bool showColorPalette = false;      // Show in color palette
    bool showWidthControl = false;      // Show width slider
    bool showColorWidthWidget = false;  // Show unified color/width widget

    // UI configuration
    bool showSeparatorBefore = false;
    QColor iconColorOverride;  // Empty = use default

    /**
     * @brief Check if this is a drawing tool that creates annotations.
     */
    bool isDrawingTool() const {
        return category == ToolCategory::Drawing;
    }

    /**
     * @brief Check if this is an action tool (one-shot operation).
     */
    bool isActionTool() const {
        return category == ToolCategory::Action;
    }

    /**
     * @brief Check if this is a toggle tool.
     */
    bool isToggleTool() const {
        return category == ToolCategory::Toggle;
    }
};

#endif // TOOLDEFINITION_H
