#include "tools/handlers/ArrowToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>

void ArrowToolHandler::onActivate(ToolContext* ctx) {
    Q_UNUSED(ctx);
    // Reset state when tool is activated
    m_isDrawing = false;
    m_currentArrow.reset();
    m_currentPolyline.reset();
}

void ArrowToolHandler::onDeactivate(ToolContext* ctx) {
    // Cancel any in-progress drawing when switching tools
    if (m_isDrawing) {
        cancelDrawing();
        ctx->repaint();
    }
}

void ArrowToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos) {
    if (ctx->polylineMode) {
        // Polyline mode: click to add points
        if (!m_isDrawing) {
            // Start a new polyline
            m_isDrawing = true;
            m_currentPolyline = std::make_unique<PolylineAnnotation>(
                ctx->color, ctx->width, LineEndStyle::None, ctx->lineStyle
            );
            m_currentPolyline->addPoint(pos);
            // Add a second point for the preview line
            m_currentPolyline->addPoint(pos);
            m_currentMousePos = pos;
            m_clickTimer.start();
        } else {
            // Check for double-click to finish
            if (m_clickTimer.isValid() && m_clickTimer.elapsed() < DOUBLE_CLICK_INTERVAL) {
                // Double-click detected - finish the polyline
                finishPolyline(ctx);
                return;
            }

            // Add a new point
            m_currentPolyline->updateLastPoint(pos);  // Confirm the preview point
            m_currentPolyline->addPoint(pos);  // Add new preview point
            m_clickTimer.restart();
        }
    } else {
        // Arrow mode: drag to draw
        m_isDrawing = true;
        m_startPoint = pos;
        // Don't create arrow yet - wait until mouse moves
        m_currentArrow.reset();
    }

    ctx->repaint();
}

void ArrowToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    if (ctx->polylineMode) {
        // Polyline mode: update the last point (preview) to follow the cursor
        if (m_currentPolyline) {
            m_currentPolyline->updateLastPoint(pos);
            m_currentMousePos = pos;
            ctx->repaint();
        }
    } else {
        // Arrow mode: create arrow only when mouse has moved enough
        if (!m_currentArrow) {
            QPoint diff = pos - m_startPoint;
            if (diff.manhattanLength() > 3) {
                m_currentArrow = std::make_unique<ArrowAnnotation>(
                    m_startPoint, pos, ctx->color, ctx->width, ctx->arrowStyle, ctx->lineStyle
                );
                ctx->repaint();
            }
        } else {
            m_currentArrow->setEnd(pos);
            ctx->repaint();
        }
    }
}

void ArrowToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos) {
    if (!m_isDrawing) {
        return;
    }

    if (ctx->polylineMode) {
        // Polyline mode: points are added on press, not release
        // Do nothing
    } else {
        // Arrow mode: finish the arrow on release
        if (m_currentArrow) {
            m_currentArrow->setEnd(pos);

            // Only add if the arrow has some length
            QPoint diff = pos - m_startPoint;
            if (diff.manhattanLength() > 5) {
                ctx->addItem(std::move(m_currentArrow));
            }
        }

        // Reset state
        m_isDrawing = false;
        m_currentArrow.reset();
        ctx->repaint();
    }
}

void ArrowToolHandler::onDoubleClick(ToolContext* ctx, const QPoint& pos) {
    if (!ctx->polylineMode || !m_isDrawing || !m_currentPolyline) {
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

    // Reset state
    m_isDrawing = false;
    m_currentPolyline.reset();
    ctx->repaint();
}

void ArrowToolHandler::drawPreview(QPainter& painter) const {
    if (m_isDrawing) {
        if (m_currentArrow) {
            m_currentArrow->draw(painter);
        }
        if (m_currentPolyline) {
            m_currentPolyline->draw(painter);
        }
    }
}

void ArrowToolHandler::cancelDrawing() {
    m_isDrawing = false;
    m_currentArrow.reset();
    m_currentPolyline.reset();
}
