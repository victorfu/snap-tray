#ifndef TOOLWIDTHSLOT_H
#define TOOLWIDTHSLOT_H

#include "ToolId.h"

/**
 * @brief Which stored width a tool reads and writes.
 *
 * Mosaic paints an area, not a line, so it keeps its own size. Every other
 * width-capable tool shares one stroke width, matching the single width
 * control in the sub-toolbar.
 */
enum class WidthSlot {
    Stroke,
    MosaicBrush,
};

/**
 * @brief Map a tool to its width slot. Tools without an entry use Stroke.
 */
WidthSlot widthSlotForTool(ToolId toolId);

#endif // TOOLWIDTHSLOT_H
