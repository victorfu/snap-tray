#ifndef TOOLCONTEXT_H
#define TOOLCONTEXT_H

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <functional>
#include <memory>

#include "ToolId.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/MosaicStroke.h"


class AnnotationItem;

/**
 * @brief Shared context passed to tool handlers.
 *
 * Contains all shared state that tool handlers need to perform
 * their operations, including the annotation layer, current settings,
 * and callback functions.
 */
class ToolContext {
public:
    // Annotation layer for adding completed items
    AnnotationLayer* annotationLayer = nullptr;

    // Current drawing settings
    QColor color = Qt::red;
    int width = 3;
    LineEndStyle arrowStyle = LineEndStyle::EndArrow;
    LineStyle lineStyle = LineStyle::Solid;

    // Shape tool settings (initialized in ToolManager.cpp since forward declared)
    int shapeType = 0;      // 0 = Rectangle, 1 = Ellipse
    int shapeFillMode = 0;  // 0 = Outline, 1 = Filled

    // Mosaic tool settings
    MosaicStroke::BlurType mosaicBlurType = MosaicStroke::BlurType::Pixelate;

    // Source pixmap (for mosaic tool)
    const QPixmap* sourcePixmap = nullptr;

    // Device pixel ratio for high-DPI support
    qreal devicePixelRatio = 1.0;

    // Drawing state
    bool isDrawing = false;
    QPoint drawStartPoint;

    // Callbacks
    std::function<void()> requestRepaint;
    std::function<void(std::unique_ptr<AnnotationItem>)> addAnnotation;

    /**
     * @brief Add an annotation to the layer.
     *
     * Convenience method that uses either the callback or direct layer access.
     */
    void addItem(std::unique_ptr<AnnotationItem> item) {
        if (addAnnotation) {
            addAnnotation(std::move(item));
        } else if (annotationLayer) {
            annotationLayer->addItem(std::move(item));
        }
    }

    /**
     * @brief Request a repaint of the canvas.
     */
    void repaint() {
        if (requestRepaint) {
            requestRepaint();
        }
    }
};

#endif // TOOLCONTEXT_H
