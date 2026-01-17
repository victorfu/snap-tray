#include "tools/handlers/ArrowToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QDebug>

void ArrowToolHandler::onActivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    // Reset all state when tool is activated
    m_isDrawing = false;
    m_hasDragged = false;
    m_isPolylineMode = false;
    m_currentArrow.reset();
    m_currentPolyline.reset();
}

void ArrowToolHandler::onDeactivate(ToolContext* ctx) {
    // Cancel any in-progress drawing when switching tools
    if (m_isDrawing || m_isPolylineMode) {
        cancelDrawing();
        ctx->repaint();
    }
}

void ArrowToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    if (m_isPolylineMode) {
        // Already in polyline mode - check for double-click to finish
        if (m_clickTimer.isValid() && m_clickTimer.elapsed() < DOUBLE_CLICK_INTERVAL) {
            // Double-click detected - finish the polyline
            m_currentPolyline->updateLastPoint(pos);
            finishPolyline(ctx);
            return;
        }

        // Add a new point (confirm the preview point and add new preview)
        m_currentPolyline->updateLastPoint(pos);  // Confirm current preview position
        m_currentPolyline->addPoint(pos);  // Add new preview point
        m_clickTimer.restart();
        ctx->repaint();
    } else {
        // Start new drawing - could become drag (arrow) or click (polyline)
        m_isDrawing = true;
        m_hasDragged = false;
        m_startPoint = pos;
        m_currentArrow.reset();
        ctx->repaint();
    }
}

void ArrowToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (m_isPolylineMode) {
        // Update the preview point to follow cursor
        if (m_currentPolyline) {
            m_currentPolyline->updateLastPoint(pos);
            ctx->repaint();
        }
        return;
    }

    if (!m_isDrawing) {
        return;
    }

    // Check if mouse has moved enough to be considered a drag
    QPoint diff = pos - m_startPoint;
    if (diff.manhattanLength() > 3) {
        m_hasDragged = true;

        // Create or update straight arrow
        if (!m_currentArrow) {
            m_currentArrow = std::make_unique<ArrowAnnotation>(
                m_startPoint, pos, ctx->color, ctx->width, ctx->arrowStyle, ctx->lineStyle
            );
        } else {
            m_currentArrow->setEnd(pos);
        }
        ctx->repaint();
    }
}

void ArrowToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (m_isPolylineMode) {
        // In polyline mode, points are added on press, not release
        return;
    }

    if (!m_isDrawing) {
        qDebug() << "ArrowToolHandler::onMouseRelease: not drawing, returning";
        return;
    }

    if (m_hasDragged) {
        // This was a drag - finalize the arrow
        QPoint diff = pos - m_startPoint;
        qDebug() << "ArrowToolHandler::onMouseRelease: hasDragged, diff.manhattanLength:" << diff.manhattanLength();
        if (diff.manhattanLength() > 5) {
            if (m_currentArrow) {
                m_currentArrow->setEnd(pos);
                qDebug() << "ArrowToolHandler: ADDING ARROW to layer! start:" << m_startPoint << "end:" << pos;
                ctx->addItem(std::move(m_currentArrow));
            }
        }

        // Reset to initial state
        m_isDrawing = false;
        m_hasDragged = false;
        m_currentArrow.reset();
    } else {
        // This was a click (no significant drag) - enter polyline mode
        m_isPolylineMode = true;
        m_isDrawing = false;  // No longer in initial drawing state

        // Create polyline with the arrow style for the last segment
        m_currentPolyline = std::make_unique<PolylineAnnotation>(
            ctx->color, ctx->width, ctx->arrowStyle, ctx->lineStyle
        );
        m_currentPolyline->addPoint(m_startPoint);  // First point
        m_currentPolyline->addPoint(pos);  // Preview point (same as start initially)

        m_clickTimer.restart();
    }

    ctx->repaint();
}

void ArrowToolHandler::onDoubleClick(ToolContext* ctx, const QPoint& pos) {
    if (!m_isPolylineMode || !m_currentPolyline) {
        return;
    }

    // Update the last point to the double-click position
    m_currentPolyline->updateLastPoint(pos);
    finishPolyline(ctx);
}

void ArrowToolHandler::finishPolyline(ToolContext* ctx) {
    if (!m_currentPolyline) {
        return;
    }

    // Remove the preview point (the last point that follows the cursor)
    m_currentPolyline->removeLastPoint();

    // Only add if we have at least 2 points
    if (m_currentPolyline->pointCount() >= 2) {
        ctx->addItem(std::move(m_currentPolyline));
    }

    // Reset all state
    m_isDrawing = false;
    m_hasDragged = false;
    m_isPolylineMode = false;
    m_currentPolyline.reset();
    ctx->repaint();
}

void ArrowToolHandler::drawPreview(QPainter& painter) const {
    if (m_currentArrow) {
        m_currentArrow->draw(painter);
    }
    if (m_currentPolyline) {
        m_currentPolyline->draw(painter);
    }
}

void ArrowToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_hasDragged = false;
    m_isPolylineMode = false;
    m_currentArrow.reset();
    m_currentPolyline.reset();
}
