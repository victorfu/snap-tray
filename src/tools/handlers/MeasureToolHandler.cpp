#include "tools/handlers/MeasureToolHandler.h"
#include "tools/ToolContext.h"

#include <QPainter>
#include <QPen>
#include <QFontMetrics>
#include <QtMath>

void MeasureToolHandler::onActivate(ToolContext* ctx)
{
    Q_UNUSED(ctx);
    m_isDrawing = false;
}

void MeasureToolHandler::onDeactivate(ToolContext* ctx)
{
    cancelDrawing();
    m_hasCompletedMeasurement = false;
    ctx->repaint();
}

void MeasureToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    m_hasCompletedMeasurement = false;
    m_isDrawing = true;
    m_startPoint = pos;
    m_endPoint = pos;
    ctx->repaint();
}

void MeasureToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing) return;
    m_endPoint = constrainEndpoint(m_startPoint, pos, ctx->shiftPressed);
    ctx->repaint();
}

void MeasureToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    if (!m_isDrawing) return;
    m_endPoint = constrainEndpoint(m_startPoint, pos, ctx->shiftPressed);
    m_isDrawing = false;

    QPoint diff = m_endPoint - m_startPoint;
    if (diff.manhattanLength() > kMinMeasureDistance) {
        m_hasCompletedMeasurement = true;
        m_completedStart = m_startPoint;
        m_completedEnd = m_endPoint;
    }
    ctx->repaint();
}

QPoint MeasureToolHandler::constrainEndpoint(const QPoint& start, const QPoint& end, bool shiftHeld) const
{
    if (!shiftHeld) return end;

    int dx = qAbs(end.x() - start.x());
    int dy = qAbs(end.y() - start.y());

    if (dx >= dy) {
        return QPoint(end.x(), start.y());
    } else {
        return QPoint(start.x(), end.y());
    }
}

void MeasureToolHandler::drawPreview(QPainter& painter) const
{
    if (m_isDrawing) {
        drawMeasurementOverlay(painter, m_startPoint, m_endPoint);
    } else if (m_hasCompletedMeasurement) {
        drawMeasurementOverlay(painter, m_completedStart, m_completedEnd);
    }
}

void MeasureToolHandler::drawMeasurementOverlay(QPainter& painter,
                                                 const QPoint& p1,
                                                 const QPoint& p2) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    const QColor measureColor(0, 122, 255);
    const QColor guideColor(0, 122, 255, 100);

    // Measurement line
    painter.setPen(QPen(measureColor, 2, Qt::SolidLine));
    painter.drawLine(p1, p2);

    // Endpoint circles
    painter.setBrush(measureColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(p1, kEndpointRadius, kEndpointRadius);
    painter.drawEllipse(p2, kEndpointRadius, kEndpointRadius);

    // Dashed dx/dy guide lines (right-angle triangle)
    painter.setPen(QPen(guideColor, 1, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    QPoint corner(p2.x(), p1.y());
    painter.drawLine(p1, corner);
    painter.drawLine(corner, p2);

    // Calculate distances
    int dx = p2.x() - p1.x();
    int dy = p2.y() - p1.y();
    double diagonal = std::sqrt(static_cast<double>(dx * dx + dy * dy));

    // Label text
    QString label = QString("dx: %1  dy: %2  d: %3 px")
                        .arg(qAbs(dx))
                        .arg(qAbs(dy))
                        .arg(diagonal, 0, 'f', 1);

    QFont font = painter.font();
    font.setPixelSize(kLabelFontSize);
    painter.setFont(font);
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(label);

    // Label position: below midpoint of measurement line
    QPoint midpoint((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2);
    int labelX = midpoint.x() - textRect.width() / 2;
    int labelY = midpoint.y() + kLabelOffsetY;

    // Background rounded rect
    QRect bgRect(labelX - kLabelPaddingH,
                 labelY - textRect.height() - kLabelPaddingV,
                 textRect.width() + kLabelPaddingH * 2,
                 textRect.height() + kLabelPaddingV * 2 + 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(bgRect, kLabelRadius, kLabelRadius);

    // Label text
    painter.setPen(Qt::white);
    painter.drawText(labelX, labelY, label);

    painter.restore();
}

void MeasureToolHandler::cancelDrawing()
{
    m_isDrawing = false;
}

bool MeasureToolHandler::handleEscape(ToolContext* ctx)
{
    if (m_isDrawing) {
        cancelDrawing();
        ctx->repaint();
        return true;
    }
    if (m_hasCompletedMeasurement) {
        m_hasCompletedMeasurement = false;
        ctx->repaint();
        return true;
    }
    return false;
}
