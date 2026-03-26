#ifndef TOOLREPAINTHELPER_H
#define TOOLREPAINTHELPER_H

#include <QRect>

#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"

namespace snaptray::tools {

inline QRect previewRepaintRect(const ToolManager* toolManager, int margin = 0)
{
    if (!toolManager) {
        return {};
    }

    auto* handler = const_cast<ToolManager*>(toolManager)->handler(toolManager->currentTool());
    if (!handler) {
        return {};
    }

    const QRect previewBounds = handler->previewBounds();
    if (!previewBounds.isValid() || previewBounds.isEmpty()) {
        return {};
    }

    return previewBounds.adjusted(-margin, -margin, margin, margin);
}

inline QRect selectedAnnotationRepaintRect(const AnnotationLayer* annotationLayer, int margin = 0)
{
    if (!annotationLayer) {
        return {};
    }

    auto* selectedItem = const_cast<AnnotationLayer*>(annotationLayer)->selectedItem();
    if (!selectedItem || !selectedItem->isVisible()) {
        return {};
    }

    const QRect bounds = selectedItem->boundingRect();
    if (!bounds.isValid() || bounds.isEmpty()) {
        return {};
    }

    return bounds.adjusted(-margin, -margin, margin, margin);
}

} // namespace snaptray::tools

#endif // TOOLREPAINTHELPER_H
