#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include <QMap>
#include <QVector>
#include <QString>

#include "ToolId.h"
#include "ToolDefinition.h"

/**
 * @brief Toolbar context types.
 *
 * Different contexts may show different subsets of tools.
 */
enum class ToolbarType {
    RegionSelector,  // Full toolbar for screenshot region selection
    ScreenCanvas,    // Simplified toolbar for screen canvas mode
    PinWindow        // Floating toolbar for pin-window annotation
};

/**
 * @brief Singleton registry for tool definitions.
 *
 * Provides centralized access to tool metadata and helper methods
 * for tool classification.
 */
class ToolRegistry {
public:
    /**
     * @brief Get the singleton instance.
     */
    static ToolRegistry& instance();

    /**
     * @brief Get the definition for a specific tool.
     */
    const ToolDefinition& get(ToolId id) const;

    /**
     * @brief Get tool IDs for a specific toolbar context.
     */
    QVector<ToolId> getToolsForToolbar(ToolbarType type) const;

    /**
     * @brief Check if a tool is a drawing tool (creates annotations).
     */
    bool isDrawingTool(ToolId id) const;

    /**
     * @brief Check if a tool is an action tool (one-shot operation).
     */
    bool isActionTool(ToolId id) const;

    /**
     * @brief Check if a tool is a toggle tool.
     */
    bool isToggleTool(ToolId id) const;

    /**
     * @brief Check if a tool is an annotation tool (stays active, supports color/width).
     */
    bool isAnnotationTool(ToolId id) const;

    /**
     * @brief Get the icon key for a tool.
     */
    QString getIconKey(ToolId id) const;

    /**
     * @brief Get the tooltip for a tool.
     */
    QString getTooltip(ToolId id) const;

    /**
     * @brief Get the tooltip including shortcut text when available.
     */
    QString getTooltipWithShortcut(ToolId id) const;

    /**
     * @brief Check if tool should show color palette in sub-toolbar.
     */
    bool showColorPalette(ToolId id) const;

    /**
     * @brief Check if tool should show width control in sub-toolbar.
     */
    bool showWidthControl(ToolId id) const;

    /**
     * @brief Check if tool should show unified color/width widget.
     */
    bool showColorWidthWidget(ToolId id) const;

private:
    ToolRegistry();
    ~ToolRegistry() = default;

    // Disable copy and move
    ToolRegistry(const ToolRegistry&) = delete;
    ToolRegistry& operator=(const ToolRegistry&) = delete;

    void registerTools();
    void registerTool(const ToolDefinition& def);

    QMap<ToolId, ToolDefinition> m_definitions;
    ToolDefinition m_defaultDefinition;  // Fallback for unknown tools
};

#endif // TOOLREGISTRY_H
