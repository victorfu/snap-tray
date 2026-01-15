#include "tools/handlers/ShapeToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

// Shape types: 0 = Rectangle, 1 = Ellipse
// Fill modes:  0 = Outline, 1 = Filled
static constexpr int kShapeRectangle = 0;
static constexpr int kFillFilled = 1;

void ShapeToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_startPoint = pos;
    m_shapeType = (ctx->shapeType == kShapeRectangle) ? ShapeType::Rectangle : ShapeType::Ellipse;
    m_fillMode = ctx->shapeFillMode;

    bool filled = (m_fillMode == kFillFilled);
    QRect rect(pos, pos);

    m_currentShape = std::make_unique<ShapeAnnotation>(
        rect, m_shapeType, ctx->color, ctx->width, filled
    );

    ctx->repaint();
}

void ShapeToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(pos);
    ctx->repaint();
}

void ShapeToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    updateCurrentShape(pos);

    QRect rect = makeRect(m_startPoint, pos);

    // Only add if the shape has some size
    if (rect.width() > 5 || rect.height() > 5) {
        if (m_currentShape) {
            ctx->addItem(std::move(m_currentShape));
        }
    }

    // Reset state
    m_isDrawing = false;
    m_currentShape.reset();

    ctx->repaint();
}

void ShapeToolHandler::drawPreview(QPainter& painter) const {
    if (!m_isDrawing || !m_currentShape) {
        return;
    }

    m_currentShape->draw(painter);
}

void ShapeToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentShape.reset();
}

QRect ShapeToolHandler::makeRect(const QPoint& start, const QPoint& end) const {
    return QRect(
        qMin(start.x(), end.x()),
        qMin(start.y(), end.y()),
        qAbs(end.x() - start.x()),
        qAbs(end.y() - start.y())
    );
}

void ShapeToolHandler::updateCurrentShape(const QPoint& endPos) {
    if (m_currentShape) {
        QRect rect = makeRect(m_startPoint, endPos);
        m_currentShape->setRect(rect);
    }
}
