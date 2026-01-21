#include "tools/ToolSectionConfig.h"
#include "toolbar/ToolOptionsPanel.h"

#include <map>

namespace {

// Data-driven tool section configurations
// Key: ToolId, Value: {color, width, text, arrowStyle, lineStyle, shape, size, autoBlur}
const std::map<ToolId, ToolSectionConfig> kToolSectionConfigs = {
    // Pencil: color, width, line style
    {ToolId::Pencil, {true, true, false, false, true, false, false, false}},

    // Marker: color only (no width control)
    {ToolId::Marker, {true, false, false, false, false, false, false, false}},

    // Arrow: color, width, arrow style, line style
    {ToolId::Arrow, {true, true, false, true, true, false, false, false}},

    // Shape: color, width, shape section
    {ToolId::Shape, {true, true, false, false, false, true, false, false}},

    // Text: color, text section
    {ToolId::Text, {true, false, true, false, false, false, false, false}},

    // Mosaic: width, auto blur section (no color)
    {ToolId::Mosaic, {false, true, false, false, false, false, false, true}},

    // StepBadge: color, size section
    {ToolId::StepBadge, {true, false, false, false, false, false, true, false}},

    // Polyline: color, width, line style (same as Pencil)
    {ToolId::Polyline, {true, true, false, false, true, false, false, false}},
};

} // anonymous namespace

ToolSectionConfig ToolSectionConfig::forTool(ToolId toolId)
{
    auto it = kToolSectionConfigs.find(toolId);
    if (it != kToolSectionConfigs.end()) {
        return it->second;
    }
    // Return empty config for tools without sub-toolbar (Eraser, EmojiSticker, actions, etc.)
    return ToolSectionConfig{};
}

void ToolSectionConfig::applyTo(ToolOptionsPanel* widget) const
{
    if (!widget) {
        return;
    }

    widget->setShowColorSection(showColorSection);
    widget->setShowWidthSection(showWidthSection);
    widget->setShowTextSection(showTextSection);
    widget->setShowArrowStyleSection(showArrowStyleSection);
    widget->setShowLineStyleSection(showLineStyleSection);
    widget->setShowShapeSection(showShapeSection);
    widget->setShowSizeSection(showSizeSection);
    widget->setShowAutoBlurSection(showAutoBlurSection);
}

bool ToolSectionConfig::hasAnySectionEnabled() const
{
    return showColorSection || showWidthSection || showTextSection ||
           showArrowStyleSection || showLineStyleSection || showShapeSection ||
           showSizeSection || showAutoBlurSection;
}
