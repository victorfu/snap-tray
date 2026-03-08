#ifndef TOOLSECTIONCONFIG_H
#define TOOLSECTIONCONFIG_H

#include "ToolId.h"

/**
 * @brief Configuration for which sub-toolbar sections to show for a tool.
 *
 * Provides a data-driven approach to configure the sub-toolbar sections based
 * on the selected tool, eliminating duplicate configuration logic across
 * RegionSelector, ScreenCanvas, and PinWindow.
 */
struct ToolSectionConfig {
    bool showColorSection = false;
    bool showWidthSection = false;
    bool showTextSection = false;
    bool showArrowStyleSection = false;
    bool showLineStyleSection = false;
    bool showShapeSection = false;
    bool showSizeSection = false;
    bool showAutoBlurSection = false;

    /**
     * @brief Get the section configuration for a tool.
     * @param toolId The tool to get configuration for
     * @return Configuration with appropriate sections enabled
     */
    static ToolSectionConfig forTool(ToolId toolId);

    /**
     * @brief Check if any section is enabled.
     * @return true if at least one section should be shown
     */
    bool hasAnySectionEnabled() const;
};

#endif // TOOLSECTIONCONFIG_H
