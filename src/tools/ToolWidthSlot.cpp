#include "tools/ToolWidthSlot.h"

#include <array>
#include <map>

namespace {

const std::map<ToolId, WidthSlot> kWidthSlotOverrides = {
    {ToolId::Mosaic, WidthSlot::MosaicBrush},
};

constexpr std::array kMosaicWidthPresets = {
    ToolWidthDefaults::kMosaicBrushSmall,
    ToolWidthDefaults::kMosaicBrush,
    ToolWidthDefaults::kMosaicBrushLarge,
};

} // namespace

WidthSlot widthSlotForTool(ToolId toolId)
{
    const auto it = kWidthSlotOverrides.find(toolId);
    return it != kWidthSlotOverrides.end() ? it->second : WidthSlot::Stroke;
}

bool isMosaicWidthPreset(int width)
{
    for (const int preset : kMosaicWidthPresets) {
        if (width == preset) {
            return true;
        }
    }
    return false;
}
