#include "tools/ToolWidthSlot.h"

#include <map>

namespace {

const std::map<ToolId, WidthSlot> kWidthSlotOverrides = {
    {ToolId::Mosaic, WidthSlot::MosaicBrush},
};

} // namespace

WidthSlot widthSlotForTool(ToolId toolId)
{
    const auto it = kWidthSlotOverrides.find(toolId);
    return it != kWidthSlotOverrides.end() ? it->second : WidthSlot::Stroke;
}
