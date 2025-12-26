#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QCursor>
#include <QPixmap>
#include <QtMath>

void EraserToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    m_isDrawing = true;
    m_eraserPath.clear();
    m_erasedItems.clear();
    m_lastPoint = pos;
    m_eraserPath.append(pos);

    eraseAtPoint(ctx, pos);
    ctx->repaint();
}

void EraserToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    m_eraserPath.append(pos);
    eraseAtPoint(ctx, pos);
    m_lastPoint = pos;
    ctx->repaint();
}

void EraserToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    eraseAtPoint(ctx, pos);

    // If any items were erased, add an ErasedItemsGroup for undo support
    if (!m_erasedItems.empty() && ctx->annotationLayer) {
        auto group = std::make_unique<ErasedItemsGroup>(std::move(m_erasedItems));
        ctx->addItem(std::move(group));
    }

    // Reset state
    m_isDrawing = false;
    m_eraserPath.clear();
    m_erasedItems.clear();

    ctx->repaint();
}

void EraserToolHandler::drawPreview(QPainter& painter) const {
    // Draw preview when hovering or actively erasing
    if (!m_hasHoverPoint && !m_isDrawing) {
        return;
    }

    QPoint drawPoint = m_isDrawing ? m_lastPoint : m_hoverPoint;
    int radius = m_eraserWidth / 2;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_isDrawing) {
        // Active erasing: solid border with slight fill
        painter.setPen(QPen(QColor(100, 100, 100), 1, Qt::SolidLine));
        painter.setBrush(QColor(128, 128, 128, 30));
    }
    else {
        // Hover: light gray dashed border
        painter.setPen(QPen(QColor(128, 128, 128, 180), 1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
    }

    painter.drawEllipse(drawPoint, radius, radius);
    painter.restore();
}

void EraserToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_eraserPath.clear();
    m_erasedItems.clear();
}

QCursor EraserToolHandler::cursor() const {
    // Return cached cursor if width hasn't changed
    if (m_cachedCursorWidth == m_eraserWidth && !m_cachedCursor.pixmap().isNull()) {
        return m_cachedCursor;
    }

    // Generate new cursor
    QPixmap pixmap = createCursorPixmap();
    int hotspot = pixmap.width() / 2;
    m_cachedCursor = QCursor(pixmap, hotspot, hotspot);
    m_cachedCursorWidth = m_eraserWidth;

    return m_cachedCursor;
}

QPixmap EraserToolHandler::createCursorPixmap() const {
    // Create cursor pixmap with eraser circle
    int size = m_eraserWidth + 4;  // Add margin for border
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int center = size / 2;
    int radius = m_eraserWidth / 2;

    // Draw semi-transparent fill
    painter.setBrush(QColor(200, 200, 200, 40));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(center, center), radius, radius);

    // Draw circle border
    painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(center, center), radius, radius);

    return pixmap;
}

void EraserToolHandler::setWidth(int width) {
    m_eraserWidth = qBound(kMinWidth, width, kMaxWidth);
}

void EraserToolHandler::setHoverPoint(const QPoint& pos) {
    m_hoverPoint = pos;
    m_hasHoverPoint = true;
}

void EraserToolHandler::clearHoverPoint() {
    m_hasHoverPoint = false;
}

void EraserToolHandler::eraseAtPoint(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->annotationLayer) {
        return;
    }

    // Remove items that intersect with the eraser
    auto removed = ctx->annotationLayer->removeItemsIntersecting(pos, m_eraserWidth);

    // Collect all erased items for undo support
    for (auto& item : removed) {
        m_erasedItems.push_back(std::move(item));
    }
}
