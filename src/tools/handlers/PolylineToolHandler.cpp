#include "tools/handlers/PolylineToolHandler.h"
#include "tools/ToolContext.h"
#include "utils/AngleSnap.h"

#include <QPainter>

void PolylineToolHandler::onActivate(ToolContext* ctx)
{
    Q_UNUSED(ctx);
    // Reset state when tool is activated
    m_isDrawing = false;
    m_currentPolyline.reset();
}

void PolylineToolHandler::onDeactivate(ToolContext* ctx)
{
    // Cancel any in-progress drawing when switching tools
    if (m_isDrawing) {
        cancelDrawing();
        ctx->repaint();
    }
}

void PolylineToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
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
            // Apply angle snapping if Shift is held
            QPoint snappedPos = pos;
            if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
                QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
                snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
            }
            m_currentPolyline->updateLastPoint(snappedPos);
            finishPolyline(ctx);
            return;
        }

        // Add a new point
        // Apply angle snapping if Shift is held
        QPoint snappedPos = pos;
        if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
            QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
            snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
        }
        m_currentPolyline->updateLastPoint(snappedPos);  // Confirm the preview point
        m_currentPolyline->addPoint(snappedPos);  // Add new preview point
        m_clickTimer.restart();
    }

    ctx->repaint();
}

void PolylineToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing || !m_currentPolyline) {
        return;
    }

    // Update the last point (preview) to follow the cursor
    // Apply angle snapping if Shift is held
    QPoint snappedPos = pos;
    if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
        QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
        snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
    }
    m_currentPolyline->updateLastPoint(snappedPos);
    m_currentMousePos = pos;
    ctx->repaint();
}

void PolylineToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    Q_UNUSED(ctx);
    Q_UNUSED(pos);
    // Points are added on press, not release
}

void PolylineToolHandler::onDoubleClick(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing || !m_currentPolyline) {
        return;
    }

    // Update the last point to the double-click position
    // Apply angle snapping if Shift is held
    QPoint snappedPos = pos;
    if (ctx->shiftPressed && m_currentPolyline->pointCount() >= 2) {
        QPoint lastConfirmed = m_currentPolyline->points().at(m_currentPolyline->pointCount() - 2);
        snappedPos = AngleSnap::snapTo45Degrees(lastConfirmed, pos);
    }
    m_currentPolyline->updateLastPoint(snappedPos);

    finishPolyline(ctx);
}

void PolylineToolHandler::finishPolyline(ToolContext* ctx)
{
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

void PolylineToolHandler::drawPreview(QPainter& painter) const
{
    if (m_isDrawing && m_currentPolyline) {
        m_currentPolyline->draw(painter);
    }
}

void PolylineToolHandler::cancelDrawing()
{
    m_isDrawing = false;
    m_currentPolyline.reset();
}

bool PolylineToolHandler::handleEscape(ToolContext* ctx)
{
    if (m_isDrawing) {
        finishPolyline(ctx);
        return true;
    }
    return false;
}
