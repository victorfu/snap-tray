#pragma once

#include <QPoint>
#include <QSize>

class QPainter;
class AnnotationLayer;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ShapeAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;

namespace snaptray::annotation {

struct SelectedAnnotationItems
{
    TextBoxAnnotation* text = nullptr;
    EmojiStickerAnnotation* emoji = nullptr;
    ShapeAnnotation* shape = nullptr;
    ArrowAnnotation* arrow = nullptr;
    PolylineAnnotation* polyline = nullptr;
};

void drawAnnotationVisuals(QPainter& painter,
                           AnnotationLayer* annotationLayer,
                           const QSize& canvasSize,
                           qreal devicePixelRatio,
                           const QPoint& origin,
                           bool useDirtyRegionRendering,
                           const SelectedAnnotationItems& selectedItems);

} // namespace snaptray::annotation
