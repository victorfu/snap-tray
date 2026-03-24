#include "annotation/AnnotationRenderHelper.h"

#include "TransformationGizmo.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

#include <QPainter>

namespace snaptray::annotation {

void drawAnnotationVisuals(QPainter& painter,
                           AnnotationLayer* annotationLayer,
                           const QSize& canvasSize,
                           qreal devicePixelRatio,
                           const QPoint& origin,
                           bool useDirtyRegionRendering,
                           const SelectedAnnotationItems& selectedItems)
{
    if (!annotationLayer) {
        return;
    }

    const int selectedIndex = annotationLayer->selectedIndex();
    if (useDirtyRegionRendering && selectedIndex >= 0) {
        annotationLayer->drawWithDirtyRegion(
            painter,
            canvasSize,
            devicePixelRatio,
            selectedIndex,
            origin);
    }
    else {
        annotationLayer->drawCached(
            painter,
            canvasSize,
            devicePixelRatio,
            origin);
    }

    painter.save();
    painter.translate(-QPointF(origin));
    if (selectedItems.text) {
        TransformationGizmo::draw(painter, selectedItems.text);
    }
    if (selectedItems.emoji) {
        TransformationGizmo::draw(painter, selectedItems.emoji);
    }
    if (selectedItems.shape) {
        TransformationGizmo::draw(painter, selectedItems.shape);
    }
    if (selectedItems.arrow) {
        TransformationGizmo::draw(painter, selectedItems.arrow);
    }
    if (selectedItems.polyline) {
        TransformationGizmo::draw(painter, selectedItems.polyline);
    }
    painter.restore();
}

} // namespace snaptray::annotation
