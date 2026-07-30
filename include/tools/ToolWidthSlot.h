#ifndef TOOLWIDTHSLOT_H
#define TOOLWIDTHSLOT_H

#include "ToolId.h"

namespace ToolWidthDefaults {
inline constexpr int kStroke = 3;
inline constexpr int kMosaicBrush = 18;
inline constexpr int kMosaicBrushMinimum = 1;
inline constexpr int kMosaicBrushMaximum = 30;
} // namespace ToolWidthDefaults

/**
 * @brief Selects the independently stored width used by an annotation tool.
 *
 * Mosaic paints an area rather than a line, so it has its own brush-size
 * slot. All other tools retain the shared stroke-width behavior.
 */
enum class WidthSlot {
    Stroke,
    MosaicBrush,
};

/**
 * @brief Maps a tool to its width slot. Unlisted tools use Stroke.
 */
WidthSlot widthSlotForTool(ToolId toolId);

#endif // TOOLWIDTHSLOT_H
