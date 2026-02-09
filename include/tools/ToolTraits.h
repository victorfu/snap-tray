#ifndef TOOLTRAITS_H
#define TOOLTRAITS_H

#include "tools/ToolId.h"
#include "tools/ToolRegistry.h"

namespace ToolTraits {

inline bool isDrawingTool(ToolId id)
{
    return ToolRegistry::instance().isDrawingTool(id);
}

inline bool isAnnotationTool(ToolId id)
{
    return ToolRegistry::instance().isAnnotationTool(id);
}

inline bool isToolManagerHandledTool(ToolId id)
{
    // All annotation tools (including Text) are handled by ToolManager.
    return isAnnotationTool(id);
}

} // namespace ToolTraits

#endif // TOOLTRAITS_H
