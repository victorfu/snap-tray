#include "tools/ToolWidthSlot.h"

#include <map>

namespace {

// Tools whose width is stored separately from the shared stroke width.
const std::map<ToolId, WidthSlot> kWidthSlotOverrides = {
    {ToolId::Mosaic, WidthSlot::MosaicBrush},
};

} // namespace

WidthSlot widthSlotForTool(ToolId toolId)
{
    const auto it = kWidthSlotOverrides.find(toolId);
    return it != kWidthSlotOverrides.end() ? it->second : WidthSlot::Stroke;
}
