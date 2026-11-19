#include "tools/ToolSectionConfig.h"

#include <map>

namespace {

// Data-driven tool section configurations
// Key: ToolId, Value: {color, width, text, arrowStyle, lineStyle, shape, size, autoBlur, crop, cutOut, measure}
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

    // Crop: aspect presets, size readout, apply / cancel
    {ToolId::Crop, {false, false, false, false, false, false, false, false, true}},

    // CutOut: remove / insert, band direction, size readout, apply / cancel
    {ToolId::CutOut, {false, false, false, false, false, false, false, false, false, true}},

    // Measure: manual / auto mode (#348)
    {ToolId::Measure, {false, false, false, false, false, false, false, false, false, false, true}},
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

bool ToolSectionConfig::hasAnySectionEnabled() const
{
    return showColorSection || showWidthSection || showTextSection ||
           showArrowStyleSection || showLineStyleSection || showShapeSection ||
           showSizeSection || showAutoBlurSection || showCropSection || showCutOutSection ||
           showMeasureSection;
}
