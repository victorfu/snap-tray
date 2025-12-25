#include "tools/handlers/ShapeToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

// Shape types: 0 = Rectangle, 1 = Ellipse
// Fill modes:  0 = Outline, 1 = Filled
static constexpr int kShapeRectangle = 0;
static constexpr int kShapeEllipse = 1;
static constexpr int kFillOutline = 0;
static constexpr int kFillFilled = 1;

void ShapeToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_startPoint = pos;
    m_shapeType = ctx->shapeType;
    m_fillMode = ctx->shapeFillMode;

    bool filled = (m_fillMode == kFillFilled);
    QRect rect(pos, pos);

    if (m_shapeType == kShapeRectangle) {
        m_currentRectangle = std::make_unique<RectangleAnnotation>(
            rect, ctx->color, ctx->width, filled
        );
        m_currentEllipse.reset();
    } else {
        m_currentEllipse = std::make_unique<EllipseAnnotation>(
            rect, ctx->color, ctx->width, filled
        );
        m_currentRectangle.reset();
    }

    ctx->repaint();
}

void ShapeToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(ctx, pos);
    ctx->repaint();
}

void ShapeToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(ctx, pos);

    QRect rect = makeRect(m_startPoint, pos);

    // Only add if the shape has some size
    if (rect.width() > 5 || rect.height() > 5) {
        if (m_shapeType == kShapeRectangle && m_currentRectangle) {
            ctx->addItem(std::move(m_currentRectangle));
        } else if (m_shapeType == kShapeEllipse && m_currentEllipse) {
            ctx->addItem(std::move(m_currentEllipse));
        }
    }

    // Reset state
    m_isDrawing = false;
    m_currentRectangle.reset();
    m_currentEllipse.reset();

    ctx->repaint();
}

void ShapeToolHandler::drawPreview(QPainter& painter) const {
    if (!m_isDrawing) {
        return;
    }

    if (m_shapeType == kShapeRectangle && m_currentRectangle) {
        m_currentRectangle->draw(painter);
    } else if (m_shapeType == kShapeEllipse && m_currentEllipse) {
        m_currentEllipse->draw(painter);
    }
}

void ShapeToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentRectangle.reset();
    m_currentEllipse.reset();
}

QRect ShapeToolHandler::makeRect(const QPoint& start, const QPoint& end) const {
    return QRect(
        qMin(start.x(), end.x()),
        qMin(start.y(), end.y()),
        qAbs(end.x() - start.x()),
        qAbs(end.y() - start.y())
    );
}

void ShapeToolHandler::updateCurrentShape(ToolContext* ctx, const QPoint& endPos) {
    Q_UNUSED(ctx);
    QRect rect = makeRect(m_startPoint, endPos);

    if (m_shapeType == kShapeRectangle && m_currentRectangle) {
        m_currentRectangle->setRect(rect);
    } else if (m_shapeType == kShapeEllipse && m_currentEllipse) {
        m_currentEllipse->setRect(rect);
    }
}
