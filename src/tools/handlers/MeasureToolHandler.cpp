#include "tools/handlers/MeasureToolHandler.h"
#include "tools/ToolContext.h"
#include "ui/DesignSystem.h"

#include <QPainter>
#include <QPen>
#include <QFontMetrics>
#include <QtMath>

#include <cmath>

namespace {

constexpr QChar kApproxSign(0x2248);   // "≈": the auto spans are estimates
constexpr QChar kMiddleDot(0x00B7);

// Logical values this close to a whole number are shown without decimals.
constexpr qreal kWholeNumberTolerance = 0.05;
// Use tool units so the pointer can approach an edge at any zoom.
constexpr qreal kSnapRadius = 6.0;
// Bound neighboring scan work when a large image is zoomed far out.
constexpr int kEndpointProbesPerSide = 8;
constexpr int kMaxBoxSideProbes = 12;
constexpr qreal kBoxSideSupportFraction = 0.5;

QString formatLogical(qreal value)
{
    // Whole numbers stay whole; fractional scales (150%) keep one decimal.
    const qreal rounded = std::round(value);
    if (qAbs(value - rounded) < kWholeNumberTolerance) {
        return QString::number(static_cast<qint64>(rounded));
    }
    return QString::number(value, 'f', 1);
}

void setLabelFont(QPainter& painter)
{
    constexpr int kLabelFontSize = 12;
    QFont font = painter.font();
    font.setPixelSize(kLabelFontSize);
    painter.setFont(font);
}

bool hasLogicalUnits(qreal imageDpr)
{
    return imageDpr > 0.0 && !qFuzzyCompare(imageDpr, 1.0);
}

QPointF snapLinePoint(const QImage& image, const QPointF& origin, const QPointF& radius,
                      Qt::Orientations axes)
{
    std::optional<QPointF> closest;
    qreal bestDistanceSquared = 1.0; // Distances are normalized to the snap radius.
    const auto inspect = [&](Qt::Orientation axis, bool checkNeighbors) {
        const bool horizontal = axis == Qt::Horizontal;
        const qreal along = horizontal ? origin.x() : origin.y();
        const qreal across = horizontal ? origin.y() : origin.x();
        const qreal alongRadius = horizontal ? radius.x() : radius.y();
        const qreal acrossRadius = horizontal ? radius.y() : radius.x();
        const auto point = [horizontal](qreal a, qreal b) {
            return horizontal ? QPointF(a, b) : QPointF(b, a);
        };
        const auto scan = [&](qreal sampleAcross, qreal snappedAcross) {
            const qreal acrossDistance = (snappedAcross - across) / acrossRadius;
            const qreal remaining = bestDistanceSquared - acrossDistance * acrossDistance;
            if (remaining < 0.0) {
                return;
            }
            if (const auto edge = MeasureEdgeScanner::nearbyEdge(
                    image, point(along, sampleAcross), axis, alongRadius * std::sqrt(remaining))) {
                const qreal alongDistance = (*edge - along) / alongRadius;
                const qreal distanceSquared = alongDistance * alongDistance
                    + acrossDistance * acrossDistance;
                if (!closest || distanceSquared < bestDistanceSquared) {
                    closest = point(*edge, snappedAcross);
                    bestDistanceSquared = distanceSquared;
                }
            }
        };
        if (!checkNeighbors) {
            scan(across, across);
            return;
        }

        // The pointer can approach a corner from outside both edges. Probe
        // neighboring pixel rows and project onto a detected edge segment.
        const int extent = horizontal ? image.height() : image.width();
        const int originLine = qFloor(across);
        const int stride = qMax(1, qFloor(acrossRadius / kEndpointProbesPerSide));
        for (const int direction : {-1, 1}) {
            const int first = direction < 0 ? qMin(extent - 1, originLine - 1)
                                            : qMax(0, originLine + 1);
            for (int line = first; line >= 0 && line < extent; line += direction * stride) {
                const qreal projection = qBound(qreal(line), across, qreal(line + 1));
                if (qAbs(projection - across) >= acrossRadius * std::sqrt(bestDistanceSquared)) {
                    break;
                }
                scan(line + 0.5, projection);
            }
        }
    };

    for (const auto axis : {Qt::Horizontal, Qt::Vertical}) {
        if (axes.testFlag(axis)) {
            inspect(axis, false);
        }
    }
    if (!closest && axes == (Qt::Horizontal | Qt::Vertical)) {
        inspect(Qt::Horizontal, true);
        inspect(Qt::Vertical, true);
    }
    // Use a point on one detected edge. Combining coordinates from two
    // unrelated scanlines can put the endpoint in empty space.
    return closest.value_or(origin);
}

qreal snapBoxSide(const QImage& image, Qt::Orientation axis, qreal position,
                  qreal from, qreal to, const QPointF& radius)
{
    const bool horizontal = axis == Qt::Horizontal;
    const qreal alongRadius = horizontal ? radius.x() : radius.y();
    const qreal acrossRadius = horizontal ? radius.y() : radius.x();
    const int extent = horizontal ? image.height() : image.width();
    const int first = qMax(0, qFloor(qMin(from, to)));
    const int last = qMin(extent - 1, qCeil(qMax(from, to)) - 1);
    if (first > last) {
        return position;
    }
    const int stride = (last - first) / kMaxBoxSideProbes + 1;
    // Both corners can be off by the snap radius. Require agreement over a
    // fraction of the remaining length, wherever the component lies along it.
    const qreal minimumLength = qMax(0.0, qAbs(to - from) - 2 * acrossRadius);
    const int minimumSupport = qMax(1, qCeil(minimumLength * kBoxSideSupportFraction / stride));

    std::optional<int> best;
    std::optional<int> previous;
    int bestSupport = minimumSupport - 1;
    int support = 0;
    for (int line = first + stride / 2; line <= last; line += stride) {
        const qreal across = line + 0.5;
        const QPointF point = horizontal ? QPointF(position, across) : QPointF(across, position);
        const auto edge = MeasureEdgeScanner::nearbyEdge(image, point, axis, alongRadius);
        support = edge && edge == previous ? support + 1 : (edge ? 1 : 0);
        previous = edge;
        if (support > bestSupport
            || (edge && best && support == bestSupport && qAbs(*edge - position) < qAbs(*best - position))) {
            best = edge;
            bestSupport = support;
        }
    }
    return best ? *best : position;
}

} // namespace

void MeasureToolHandler::onActivate(ToolContext* ctx)
{
    Q_UNUSED(ctx);
    m_isDrawing = false;
}

void MeasureToolHandler::onDeactivate(ToolContext* ctx)
{
    cancelDrawing();
    m_hasCompletedMeasurement = false;
    clearHover();
    releaseRaster();
    ctx->repaint();
}

void MeasureToolHandler::onMousePress(ToolContext* ctx, const QPoint& pos)
{
    onMousePressF(ctx, QPointF(pos));
}

void MeasureToolHandler::onMouseMove(ToolContext* ctx, const QPoint& pos)
{
    onMouseMoveF(ctx, QPointF(pos));
}

void MeasureToolHandler::onMouseRelease(ToolContext* ctx, const QPoint& pos)
{
    onMouseReleaseF(ctx, QPointF(pos));
}

void MeasureToolHandler::onMousePressF(ToolContext* ctx, const QPointF& pos)
{
    m_hasCompletedMeasurement = false;
    clearHover();
    m_isDrawing = true;
    m_isBox = m_autoMode && ctx->pressModifiers.testFlag(Qt::ControlModifier);
    m_isEstimate = false;
    m_rawStart = toImage(pos);
    m_startPoint = m_rawStart;
    m_endPoint = m_startPoint;
    ctx->repaint();
}

void MeasureToolHandler::onMouseMoveF(ToolContext* ctx, const QPointF& pos)
{
    if (!m_isDrawing) {
        if (m_autoMode && updateHover(pos)) {
            ctx->repaint();
        }
        return;
    }
    updateDrag(toImage(pos), ctx->shiftPressed);
    ctx->repaint();
}

void MeasureToolHandler::onMouseReleaseF(ToolContext* ctx, const QPointF& pos)
{
    if (!m_isDrawing) return;
    m_isDrawing = false;

    // The minimum is on screen (tool units): a click is not a measurement.
    const QPointF imageEnd = toImage(pos);
    const QPointF rawEnd = constrainEndpoint(m_rawStart, imageEnd, !m_isBox && ctx->shiftPressed);
    const QPointF diff = toTool(rawEnd - m_rawStart);
    if (diff.manhattanLength() > kMinMeasureDistance) {
        updateDrag(imageEnd, ctx->shiftPressed);
        m_hasCompletedMeasurement = !m_isBox || !QRectF(m_startPoint, m_endPoint).normalized().isEmpty();
        m_completedStart = m_startPoint;
        m_completedEnd = m_endPoint;
    }
    ctx->repaint();
}

void MeasureToolHandler::updateDrag(const QPointF& imageEnd, bool shiftHeld)
{
    const bool constrain = !m_isBox && shiftHeld;
    const QPointF rawEnd = constrainEndpoint(m_rawStart, imageEnd, constrain);
    m_startPoint = m_rawStart;
    m_endPoint = rawEnd;
    m_isEstimate = false;
    if (!m_autoMode || !hasImageGeometry() || !m_rasterProvider || rawEnd == m_rawStart) {
        return;
    }
    const auto raster = m_rasterProvider();
    if (!raster || raster->size() != m_pixelSize) {
        return;
    }

    const QPointF radius = toImage(QPointF(kSnapRadius, kSnapRadius));
    if (m_isBox) {
        m_startPoint.setX(snapBoxSide(*raster, Qt::Horizontal, m_rawStart.x(),
                                     m_rawStart.y(), rawEnd.y(), radius));
        m_endPoint.setX(snapBoxSide(*raster, Qt::Horizontal, rawEnd.x(),
                                   m_rawStart.y(), rawEnd.y(), radius));
        m_startPoint.setY(snapBoxSide(*raster, Qt::Vertical, m_rawStart.y(),
                                     m_rawStart.x(), rawEnd.x(), radius));
        m_endPoint.setY(snapBoxSide(*raster, Qt::Vertical, rawEnd.y(),
                                   m_rawStart.x(), rawEnd.x(), radius));
        // Nearby sides can find the same edge. Keep the original extent on
        // that axis if snapping would collapse the box or invert its sides.
        if ((m_endPoint.x() - m_startPoint.x()) * (rawEnd.x() - m_rawStart.x()) <= 0) {
            m_startPoint.setX(m_rawStart.x());
            m_endPoint.setX(rawEnd.x());
        }
        if ((m_endPoint.y() - m_startPoint.y()) * (rawEnd.y() - m_rawStart.y()) <= 0) {
            m_startPoint.setY(m_rawStart.y());
            m_endPoint.setY(rawEnd.y());
        }
    } else {
        Qt::Orientations axes = Qt::Horizontal | Qt::Vertical;
        if (constrain) {
            axes = rawEnd.y() == m_rawStart.y() ? Qt::Horizontal : Qt::Vertical;
        }
        m_startPoint = snapLinePoint(*raster, m_rawStart, radius, axes);
        m_endPoint = snapLinePoint(*raster, rawEnd, radius, axes);
        if (m_endPoint == m_startPoint) {
            m_startPoint = m_rawStart;
            m_endPoint = rawEnd;
        }
    }
    m_isEstimate = m_startPoint != m_rawStart || m_endPoint != rawEnd;
}

QPointF MeasureToolHandler::constrainEndpoint(const QPointF& start, const QPointF& end, bool shiftHeld) const
{
    if (!shiftHeld) return end;

    const qreal dx = qAbs(end.x() - start.x());
    const qreal dy = qAbs(end.y() - start.y());

    if (dx >= dy) {
        return QPointF(end.x(), start.y());
    } else {
        return QPointF(start.x(), end.y());
    }
}

// ── Image geometry and auto mode ──

void MeasureToolHandler::setAutoMode(bool enabled)
{
    if (m_autoMode == enabled) {
        return;
    }
    m_autoMode = enabled;
    if (!enabled) {
        clearHover();
        releaseRaster();
    }
}

void MeasureToolHandler::setImageGeometry(const QSize& toolSize, const QSize& pixelSize, qreal imageDpr,
                                          qint64 imageKey)
{
    const qreal dpr = imageDpr > 0.0 ? imageDpr : 1.0;
    if (toolSize == m_toolSize && pixelSize == m_pixelSize && qFuzzyCompare(dpr, m_imageDpr)
        && imageKey == m_imageKey) {
        return;
    }
    // A zoom or rotation only changes the tool size: the line, kept in image
    // pixels, still measures the same pixels. Another image or crop does not.
    if (pixelSize != m_pixelSize || imageKey != m_imageKey) {
        if (m_isEstimate) {
            cancelDrawing();
        }
        m_hasCompletedMeasurement = false;
    }
    m_toolSize = toolSize;
    m_pixelSize = pixelSize;
    m_imageDpr = dpr;
    m_imageKey = imageKey;
    // Spans found on the previous image or scale no longer line up.
    clearHover();
}

void MeasureToolHandler::setRasterProvider(RasterProvider provider, std::function<void()> release)
{
    m_rasterProvider = std::move(provider);
    m_releaseRaster = std::move(release);
    clearHover();
}

void MeasureToolHandler::releaseRaster()
{
    m_hoverRaster.reset();
    if (m_releaseRaster) {
        m_releaseRaster();
    }
}

bool MeasureToolHandler::clearHover()
{
    const bool hadHover = hasHoverMeasurement();
    m_hoverHorizontal.reset();
    m_hoverVertical.reset();
    m_hoverRaster.reset();
    m_hoverPixel = QPoint(-1, -1);
    return hadHover;
}

std::optional<MeasureEdgeScanner::Span> MeasureToolHandler::hoverSpan(Qt::Orientation orientation) const
{
    return orientation == Qt::Horizontal ? m_hoverHorizontal : m_hoverVertical;
}

bool MeasureToolHandler::hasImageGeometry() const
{
    return !m_toolSize.isEmpty() && !m_pixelSize.isEmpty();
}

QPointF MeasureToolHandler::pixelsPerToolUnit() const
{
    if (!hasImageGeometry()) {
        return QPointF(1.0, 1.0);
    }
    return QPointF(static_cast<qreal>(m_pixelSize.width()) / m_toolSize.width(),
                   static_cast<qreal>(m_pixelSize.height()) / m_toolSize.height());
}

QPointF MeasureToolHandler::toImage(const QPointF& toolPos) const
{
    const QPointF scale = pixelsPerToolUnit();
    return QPointF(toolPos.x() * scale.x(), toolPos.y() * scale.y());
}

QPointF MeasureToolHandler::toTool(const QPointF& imagePos) const
{
    const QPointF scale = pixelsPerToolUnit();
    return QPointF(imagePos.x() / scale.x(), imagePos.y() / scale.y());
}

QPoint MeasureToolHandler::toPixel(const QPointF& toolPos) const
{
    const QPointF imagePos = toImage(toolPos);
    return QPoint(qFloor(imagePos.x()), qFloor(imagePos.y()));
}

// Returns true when what the hover shows changed.
bool MeasureToolHandler::updateHover(const QPointF& toolPos)
{
    if (!hasImageGeometry() || !m_rasterProvider) {
        return clearHover();
    }
    const QPoint pixel = toPixel(toolPos);
    if (pixel.x() < 0 || pixel.y() < 0
        || pixel.x() >= m_pixelSize.width() || pixel.y() >= m_pixelSize.height()) {
        return clearHover();
    }

    std::shared_ptr<const QImage> raster = m_rasterProvider();
    if (!raster || raster->size() != m_pixelSize
        || !MeasureEdgeScanner::isSupportedFormat(raster->format())) {
        return clearHover();
    }

    const bool hadHover = hasHoverMeasurement();
    if (pixel == m_hoverPixel && raster == m_hoverRaster) {
        // Same pixel of the same image: nothing to rescan or repaint.
        return false;
    }
    m_hoverPixel = pixel;
    m_hoverRaster = std::move(raster);

    auto reliable = [](std::optional<MeasureEdgeScanner::Span> span) {
        return span && span->isReliable() ? span : std::nullopt;
    };
    m_hoverHorizontal = reliable(MeasureEdgeScanner::scan(*m_hoverRaster, pixel, Qt::Horizontal));
    m_hoverVertical = reliable(MeasureEdgeScanner::scan(*m_hoverRaster, pixel, Qt::Vertical));
    return hadHover || hasHoverMeasurement();
}

// ── Labels ──

QString MeasureToolHandler::formatLength(int pixels, qreal imageDpr)
{
    QString text = QStringLiteral("%1 px").arg(pixels);
    if (hasLogicalUnits(imageDpr)) {
        text += QStringLiteral(" %1 %2 pt").arg(kMiddleDot, formatLogical(pixels / imageDpr));
    }
    return text;
}

QStringList MeasureToolHandler::manualLabelLines(const QPointF& deltaPixels, qreal imageDpr)
{
    const qreal dx = qAbs(deltaPixels.x());
    const qreal dy = qAbs(deltaPixels.y());
    const qreal diagonal = std::hypot(dx, dy);
    QStringList lines{QStringLiteral("dx: %1  dy: %2  d: %3 px")
                          .arg(qRound(dx))
                          .arg(qRound(dy))
                          .arg(diagonal, 0, 'f', 1)};
    if (hasLogicalUnits(imageDpr)) {
        lines.append(QStringLiteral("dx: %1  dy: %2  d: %3 pt")
                         .arg(formatLogical(dx / imageDpr),
                              formatLogical(dy / imageDpr))
                         .arg(diagonal / imageDpr, 0, 'f', 1));
    }
    return lines;
}

QStringList MeasureToolHandler::measurementLabelLines(const QPointF& deltaPixels) const
{
    QStringList lines;
    if (m_isBox) {
        const qreal width = qAbs(deltaPixels.x());
        const qreal height = qAbs(deltaPixels.y());
        lines.append(QStringLiteral("%1 \u00d7 %2 px").arg(qRound(width)).arg(qRound(height)));
        if (hasLogicalUnits(m_imageDpr)) {
            lines.append(QStringLiteral("%1 \u00d7 %2 pt").arg(formatLogical(width / m_imageDpr),
                                                               formatLogical(height / m_imageDpr)));
        }
    } else {
        lines = manualLabelLines(deltaPixels, m_imageDpr);
    }
    if (m_isEstimate) {
        for (QString& line : lines) {
            line.prepend(QString(kApproxSign) + QLatin1Char(' '));
        }
    }
    return lines;
}

// ── Drawing ──

void MeasureToolHandler::drawPreview(QPainter& painter) const
{
    if (m_isDrawing) {
        drawMeasurementOverlay(painter, m_startPoint, m_endPoint);
        return;
    }
    if (m_hasCompletedMeasurement) {
        drawMeasurementOverlay(painter, m_completedStart, m_completedEnd);
    }
    if (m_autoMode && hasHoverMeasurement()) {
        drawHoverOverlay(painter);
    }
}

QRect MeasureToolHandler::labelRect(const QFontMetrics& fm, const QStringList& lines,
                                    const QPoint& textTopCenter) const
{
    int textWidth = 0;
    for (const QString& line : lines) {
        textWidth = qMax(textWidth, fm.horizontalAdvance(line));
    }
    const QRect textRect(textTopCenter.x() - textWidth / 2, textTopCenter.y(),
                         textWidth, fm.height() * static_cast<int>(lines.size()));
    return textRect.adjusted(-kLabelPaddingH, -kLabelPaddingV, kLabelPaddingH, kLabelPaddingV);
}

QRect MeasureToolHandler::keepInsideImage(QRect rect) const
{
    if (!hasImageGeometry()) {
        return rect;
    }
    const QRect bounds(QPoint(0, 0), m_toolSize);
    if (rect.right() > bounds.right()) {
        rect.moveRight(bounds.right());
    }
    if (rect.bottom() > bounds.bottom()) {
        rect.moveBottom(bounds.bottom());
    }
    // Last, so a label wider than a tiny pin keeps its start visible.
    if (rect.left() < bounds.left()) {
        rect.moveLeft(bounds.left());
    }
    if (rect.top() < bounds.top()) {
        rect.moveTop(bounds.top());
    }
    return rect;
}

void MeasureToolHandler::paintLabel(QPainter& painter, const QStringList& lines,
                                    const QRect& backgroundRect) const
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(backgroundRect, kLabelRadius, kLabelRadius);

    const QRect textRect =
        backgroundRect.adjusted(kLabelPaddingH, kLabelPaddingV, -kLabelPaddingH, -kLabelPaddingV);
    const int lineHeight = lines.isEmpty() ? 0 : textRect.height() / static_cast<int>(lines.size());
    painter.setPen(Qt::white);
    for (int i = 0; i < lines.size(); ++i) {
        painter.drawText(QRect(textRect.x(), textRect.y() + i * lineHeight, textRect.width(), lineHeight),
                         Qt::AlignCenter, lines.at(i));
    }
}

void MeasureToolHandler::drawMeasurementOverlay(QPainter& painter,
                                                 const QPointF& imageP1,
                                                 const QPointF& imageP2) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    setLabelFont(painter);

    const QColor measureColor = DesignSystem::instance().accentDefault();
    QColor guideColor = measureColor;
    guideColor.setAlpha(100);

    // Drawn where the image pixels are now, at the current zoom.
    const QPointF p1 = toTool(imageP1);
    const QPointF p2 = toTool(imageP2);

    // Draw the snapped geometry at the pin's current zoom.
    painter.setPen(QPen(measureColor, 2, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    if (m_isBox) {
        painter.drawRect(QRectF(p1, p2).normalized());
    } else {
        painter.drawLine(p1, p2);

        painter.setBrush(measureColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(p1, kEndpointRadius, kEndpointRadius);
        painter.drawEllipse(p2, kEndpointRadius, kEndpointRadius);

        // Dashed dx/dy guide lines (right-angle triangle).
        painter.setPen(QPen(guideColor, 1, Qt::DashLine));
        const QPointF corner(p2.x(), p1.y());
        painter.drawLine(p1, corner);
        painter.drawLine(corner, p2);
    }

    const QStringList lines = measurementLabelLines(imageP2 - imageP1);
    const QPointF midpoint = (p1 + p2) / 2;
    const qreal labelY = m_isBox ? qMax(p1.y(), p2.y()) : midpoint.y();
    const QFontMetrics fm(painter.font());
    const QRect rect = keepInsideImage(
        labelRect(fm, lines, QPoint(qRound(midpoint.x()), qRound(labelY) + kLabelOffsetY - fm.ascent())));
    paintLabel(painter, lines, rect);

    painter.restore();
}

QString MeasureToolHandler::hoverLabelText(Qt::Orientation orientation) const
{
    const std::optional<MeasureEdgeScanner::Span>& span =
        orientation == Qt::Horizontal ? m_hoverHorizontal : m_hoverVertical;
    if (!span) {
        return QString();
    }
    return QString(kApproxSign) + QLatin1Char(' ') + formatLength(span->length(), m_imageDpr);
}

QRect MeasureToolHandler::hoverLabelRect(Qt::Orientation orientation, const QFontMetrics& fm) const
{
    const QPointF anchor = toTool(QPointF(m_hoverPixel.x() + 0.5, m_hoverPixel.y() + 0.5));
    const QStringList lines{hoverLabelText(orientation)};

    if (orientation == Qt::Horizontal) {
        if (!m_hoverHorizontal) {
            return QRect();
        }
        const qreal x1 = toTool(QPointF(m_hoverHorizontal->first, 0)).x();
        const qreal x2 = toTool(QPointF(m_hoverHorizontal->last + 1, 0)).x();
        const int centerX = qRound((x1 + x2) / 2);
        const int y = qRound(anchor.y());
        // Above the span line, or below it when the pin has no room above.
        QRect rect = labelRect(fm, lines, QPoint(centerX, y - kSpanLabelGap - fm.height()));
        if (hasImageGeometry() && rect.top() < 0) {
            rect = labelRect(fm, lines, QPoint(centerX, y + kSpanLabelGap + kLabelPaddingV));
        }
        return keepInsideImage(rect);
    }

    if (!m_hoverVertical) {
        return QRect();
    }
    const qreal y1 = toTool(QPointF(0, m_hoverVertical->first)).y();
    const qreal y2 = toTool(QPointF(0, m_hoverVertical->last + 1)).y();
    const int x = qRound(anchor.x());
    const int textTop = qRound((y1 + y2) / 2) - fm.height() / 2;
    const int textWidth = fm.horizontalAdvance(lines.first());
    // Right of the span line, or left of it when the pin has no room right.
    QRect rect = labelRect(fm, lines, QPoint(x + kSpanLabelGap + kLabelPaddingH + textWidth / 2, textTop));
    if (hasImageGeometry() && rect.right() > m_toolSize.width() - 1) {
        rect = labelRect(fm, lines,
                         QPoint(x - kSpanLabelGap - kLabelPaddingH - (textWidth - textWidth / 2), textTop));
    }
    return keepInsideImage(rect);
}

void MeasureToolHandler::drawHoverOverlay(QPainter& painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    setLabelFont(painter);

    const QColor measureColor = DesignSystem::instance().accentDefault();
    // Pixel edges in tool coordinates: a span covers [first, last + 1).
    // The lines run through the middle of the hovered pixel.
    const QPointF anchor = toTool(QPointF(m_hoverPixel.x() + 0.5, m_hoverPixel.y() + 0.5));
    const QFontMetrics fm(painter.font());

    if (m_hoverHorizontal) {
        const qreal y = anchor.y();
        const qreal x1 = toTool(QPointF(m_hoverHorizontal->first, 0)).x();
        const qreal x2 = toTool(QPointF(m_hoverHorizontal->last + 1, 0)).x();
        painter.setPen(QPen(measureColor, 1, Qt::SolidLine));
        painter.drawLine(QPointF(x1, y), QPointF(x2, y));
        // Ticks only on found edges; an image border is not an edge.
        if (m_hoverHorizontal->firstIsEdge) {
            painter.drawLine(QPointF(x1, y - kSpanTickHalfLength), QPointF(x1, y + kSpanTickHalfLength));
        }
        if (m_hoverHorizontal->lastIsEdge) {
            painter.drawLine(QPointF(x2, y - kSpanTickHalfLength), QPointF(x2, y + kSpanTickHalfLength));
        }
        paintLabel(painter, {hoverLabelText(Qt::Horizontal)}, hoverLabelRect(Qt::Horizontal, fm));
    }

    if (m_hoverVertical) {
        const qreal x = anchor.x();
        const qreal y1 = toTool(QPointF(0, m_hoverVertical->first)).y();
        const qreal y2 = toTool(QPointF(0, m_hoverVertical->last + 1)).y();
        painter.setPen(QPen(measureColor, 1, Qt::SolidLine));
        painter.drawLine(QPointF(x, y1), QPointF(x, y2));
        if (m_hoverVertical->firstIsEdge) {
            painter.drawLine(QPointF(x - kSpanTickHalfLength, y1), QPointF(x + kSpanTickHalfLength, y1));
        }
        if (m_hoverVertical->lastIsEdge) {
            painter.drawLine(QPointF(x - kSpanTickHalfLength, y2), QPointF(x + kSpanTickHalfLength, y2));
        }
        paintLabel(painter, {hoverLabelText(Qt::Vertical)}, hoverLabelRect(Qt::Vertical, fm));
    }

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
