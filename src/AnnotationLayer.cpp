#include "AnnotationLayer.h"
#include <QPainterPath>
#include <QtMath>
#include <QDebug>
#include <QSet>
#include <QRandomGenerator>
#include <algorithm>  // For std::fill in mosaic optimization

// ============================================================================
// Helper: Build smooth path using quadratic Bezier with midpoints
// This approach uses midpoints as curve endpoints and original points as
// control points, creating smooth curves without overshoot.
// ============================================================================

static QPainterPath buildSmoothPath(const QVector<QPointF> &points)
{
    QPainterPath path;

    if (points.size() < 2) {
        return path;
    }

    if (points.size() == 2) {
        path.moveTo(points[0]);
        path.lineTo(points[1]);
        return path;
    }

    // Start at first point
    path.moveTo(points[0]);

    // First segment: line to first midpoint (smooth start)
    QPointF mid0 = (points[0] + points[1]) / 2.0;
    path.lineTo(mid0);

    // Middle segments: midpoint to midpoint with original points as control
    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF mid = (points[i] + points[i + 1]) / 2.0;
        path.quadTo(points[i], mid);
    }

    // Last segment: curve to end point using last point as control
    path.quadTo(points[points.size() - 1], points.last());

    return path;
}

// ============================================================================
// PencilStroke Implementation
// ============================================================================

PencilStroke::PencilStroke(const QVector<QPointF> &points, const QColor &color, int width)
    : m_points(points)
    , m_color(color)
    , m_width(width)
{
}

void PencilStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    painter.save();
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path = buildSmoothPath(m_points);
    painter.drawPath(path);

    painter.restore();
}

QRect PencilStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    if (m_boundingRectDirty) {
        qreal minX = m_points[0].x();
        qreal maxX = m_points[0].x();
        qreal minY = m_points[0].y();
        qreal maxY = m_points[0].y();

        for (const QPointF &p : m_points) {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(static_cast<int>(minX) - margin, static_cast<int>(minY) - margin,
                                    static_cast<int>(maxX - minX) + 2 * margin, static_cast<int>(maxY - minY) + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> PencilStroke::clone() const
{
    return std::make_unique<PencilStroke>(m_points, m_color, m_width);
}

void PencilStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    // Incrementally update bounding rect cache
    int margin = m_width / 2 + 1;
    QRect pointRect(static_cast<int>(point.x()) - margin, static_cast<int>(point.y()) - margin,
                    margin * 2, margin * 2);

    if (m_boundingRectDirty || m_boundingRectCache.isEmpty()) {
        m_boundingRectCache = pointRect;
        m_boundingRectDirty = false;
    } else {
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }
}

QPainterPath PencilStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return QPainterPath();
    }

    // Build the stroke path with width
    QPainterPath linePath;
    linePath.moveTo(m_points[0]);
    for (int i = 1; i < m_points.size(); ++i) {
        linePath.lineTo(m_points[i]);
    }

    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    return stroker.createStroke(linePath);
}

bool PencilStroke::intersectsCircle(const QPoint &center, int radius) const
{
    // Quick bounding box check first
    QRect bbox = boundingRect();
    QRect eraserRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    if (!bbox.intersects(eraserRect)) {
        return false;
    }

    // Path-based intersection
    QPainterPath eraserPath;
    eraserPath.addEllipse(center, radius, radius);
    return strokePath().intersects(eraserPath);
}

// ============================================================================
// MarkerStroke Implementation
// ============================================================================

MarkerStroke::MarkerStroke(const QVector<QPointF> &points, const QColor &color, int width)
    : m_points(points)
    , m_color(color)
    , m_width(width)
{
}

void MarkerStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    painter.save();

    QRect bounds = boundingRect();
    if (bounds.isEmpty()) {
        painter.restore();
        return;
    }

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid
    bool cacheValid = !m_cachedPixmap.isNull()
        && m_cachedDpr == dpr
        && m_cachedPointCount == m_points.size();

    if (!cacheValid) {
        // Regenerate cache
        QPixmap offscreen(bounds.size() * dpr);
        offscreen.setDevicePixelRatio(dpr);
        offscreen.fill(Qt::transparent);

        {
            QPainter offPainter(&offscreen);
            offPainter.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            offPainter.setPen(pen);
            offPainter.setBrush(Qt::NoBrush);

            // Build smooth path and translate to local coordinates
            QPainterPath path = buildSmoothPath(m_points);
            path.translate(-bounds.topLeft());
            offPainter.drawPath(path);
        }

        // Update cache
        m_cachedPixmap = offscreen;
        m_cachedOrigin = bounds.topLeft();
        m_cachedDpr = dpr;
        m_cachedPointCount = m_points.size();
    }

    // Use cached pixmap
    painter.setOpacity(0.4);
    painter.drawPixmap(m_cachedOrigin, m_cachedPixmap);

    painter.restore();
}

QRect MarkerStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    if (m_boundingRectDirty) {
        qreal minX = m_points[0].x();
        qreal maxX = m_points[0].x();
        qreal minY = m_points[0].y();
        qreal maxY = m_points[0].y();

        for (const QPointF &p : m_points) {
            minX = qMin(minX, p.x());
            maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y());
            maxY = qMax(maxY, p.y());
        }

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(static_cast<int>(minX) - margin, static_cast<int>(minY) - margin,
                                    static_cast<int>(maxX - minX) + 2 * margin, static_cast<int>(maxY - minY) + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> MarkerStroke::clone() const
{
    return std::make_unique<MarkerStroke>(m_points, m_color, m_width);
}

void MarkerStroke::addPoint(const QPointF &point)
{
    m_points.append(point);

    // Incrementally update bounding rect cache
    int margin = m_width / 2 + 1;
    QRect pointRect(static_cast<int>(point.x()) - margin, static_cast<int>(point.y()) - margin,
                    margin * 2, margin * 2);

    if (m_boundingRectDirty || m_boundingRectCache.isEmpty()) {
        m_boundingRectCache = pointRect;
        m_boundingRectDirty = false;
    } else {
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }
}

QPainterPath MarkerStroke::strokePath() const
{
    if (m_points.size() < 2) {
        return QPainterPath();
    }

    // Build the stroke path with width
    QPainterPath linePath;
    linePath.moveTo(m_points[0]);
    for (int i = 1; i < m_points.size(); ++i) {
        linePath.lineTo(m_points[i]);
    }

    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    return stroker.createStroke(linePath);
}

bool MarkerStroke::intersectsCircle(const QPoint &center, int radius) const
{
    // Quick bounding box check first
    QRect bbox = boundingRect();
    QRect eraserRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    if (!bbox.intersects(eraserRect)) {
        return false;
    }

    // Path-based intersection
    QPainterPath eraserPath;
    eraserPath.addEllipse(center, radius, radius);
    return strokePath().intersects(eraserPath);
}

// ============================================================================
// ArrowAnnotation Implementation
// ============================================================================

ArrowAnnotation::ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width,
                                 LineEndStyle style)
    : m_start(start)
    , m_end(end)
    , m_color(color)
    , m_width(width)
    , m_lineEndStyle(style)
{
}

void ArrowAnnotation::draw(QPainter &painter) const
{
    painter.save();
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Calculate line endpoint (adjust if arrowhead is present)
    QPointF lineEnd = m_end;
    if (m_lineEndStyle != LineEndStyle::None) {
        // Move line end back to arrowhead base so line doesn't protrude through arrow
        double angle = qAtan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
        double arrowLength = qMax(15.0, m_width * 5.0);
        lineEnd = QPointF(
            m_end.x() - arrowLength * qCos(angle),
            m_end.y() - arrowLength * qSin(angle)
        );
    }

    // Draw the line (to correct endpoint)
    painter.drawLine(m_start, lineEnd.toPoint());

    // Draw arrowhead(s) based on line end style
    switch (m_lineEndStyle) {
    case LineEndStyle::None:
        // Plain line, no arrowheads
        break;
    case LineEndStyle::EndArrow:
        drawArrowhead(painter, m_start, m_end);
        break;
    }

    painter.restore();
}

void ArrowAnnotation::drawArrowhead(QPainter &painter, const QPoint &start, const QPoint &end) const
{
    // Calculate the angle of the line
    double angle = qAtan2(end.y() - start.y(), end.x() - start.x());

    // Arrowhead size proportional to line width
    double arrowLength = qMax(15.0, m_width * 5.0);
    double arrowAngle = M_PI / 6.0;  // 30 degrees

    // Calculate arrowhead points
    QPointF arrowP1(
        end.x() - arrowLength * qCos(angle - arrowAngle),
        end.y() - arrowLength * qSin(angle - arrowAngle)
    );
    QPointF arrowP2(
        end.x() - arrowLength * qCos(angle + arrowAngle),
        end.y() - arrowLength * qSin(angle + arrowAngle)
    );

    // Draw filled arrowhead
    QPainterPath arrowPath;
    arrowPath.moveTo(end);
    arrowPath.lineTo(arrowP1);
    arrowPath.lineTo(arrowP2);
    arrowPath.closeSubpath();

    painter.setBrush(m_color);
    painter.drawPath(arrowPath);
}

QRect ArrowAnnotation::boundingRect() const
{
    int margin = 20;  // Extra margin for arrowhead
    int minX = qMin(m_start.x(), m_end.x()) - margin;
    int maxX = qMax(m_start.x(), m_end.x()) + margin;
    int minY = qMin(m_start.y(), m_end.y()) - margin;
    int maxY = qMax(m_start.y(), m_end.y()) + margin;

    return QRect(minX, minY, maxX - minX, maxY - minY);
}

std::unique_ptr<AnnotationItem> ArrowAnnotation::clone() const
{
    return std::make_unique<ArrowAnnotation>(m_start, m_end, m_color, m_width, m_lineEndStyle);
}

void ArrowAnnotation::setEnd(const QPoint &end)
{
    m_end = end;
}

// ============================================================================
// RectangleAnnotation Implementation
// ============================================================================

RectangleAnnotation::RectangleAnnotation(const QRect &rect, const QColor &color, int width, bool filled)
    : m_rect(rect)
    , m_color(color)
    , m_width(width)
    , m_filled(filled)
{
}

void RectangleAnnotation::draw(QPainter &painter) const
{
    painter.save();
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_filled) {
        painter.setBrush(m_color);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    QRect normalizedRect = m_rect.normalized();
    qDebug() << "RectangleAnnotation::draw - rect:" << normalizedRect
             << "transform:" << painter.transform()
             << "mapped rect:" << painter.transform().mapRect(QRectF(normalizedRect));

    painter.drawRect(normalizedRect);

    painter.restore();
}

QRect RectangleAnnotation::boundingRect() const
{
    int margin = m_width / 2 + 1;
    return m_rect.normalized().adjusted(-margin, -margin, margin, margin);
}

std::unique_ptr<AnnotationItem> RectangleAnnotation::clone() const
{
    return std::make_unique<RectangleAnnotation>(m_rect, m_color, m_width, m_filled);
}

void RectangleAnnotation::setRect(const QRect &rect)
{
    m_rect = rect;
}

// ============================================================================
// EllipseAnnotation Implementation
// ============================================================================

EllipseAnnotation::EllipseAnnotation(const QRect &rect, const QColor &color, int width, bool filled)
    : m_rect(rect)
    , m_color(color)
    , m_width(width)
    , m_filled(filled)
{
}

void EllipseAnnotation::draw(QPainter &painter) const
{
    painter.save();
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_filled) {
        painter.setBrush(m_color);
    } else {
        painter.setBrush(Qt::NoBrush);
    }

    QRect normalizedRect = m_rect.normalized();
    painter.drawEllipse(normalizedRect);

    painter.restore();
}

QRect EllipseAnnotation::boundingRect() const
{
    int margin = m_width / 2 + 1;
    return m_rect.normalized().adjusted(-margin, -margin, margin, margin);
}

std::unique_ptr<AnnotationItem> EllipseAnnotation::clone() const
{
    return std::make_unique<EllipseAnnotation>(m_rect, m_color, m_width, m_filled);
}

void EllipseAnnotation::setRect(const QRect &rect)
{
    m_rect = rect;
}

// ============================================================================
// TextAnnotation Implementation
// ============================================================================

TextAnnotation::TextAnnotation(const QPoint &position, const QString &text, const QFont &font, const QColor &color)
    : m_position(position)
    , m_text(text)
    , m_font(font)
    , m_color(color)
{
}

QPointF TextAnnotation::center() const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // m_position is baseline position of first line
    // Center is at middle of the text bounding box
    qreal cx = m_position.x() + maxWidth / 2.0;
    qreal cy = m_position.y() - fm.ascent() + totalHeight / 2.0;

    return QPointF(cx, cy);
}

QPolygonF TextAnnotation::transformedBoundingPolygon() const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // Build untransformed bounding rect with margin for outline
    QRectF textRect(m_position.x(),
                    m_position.y() - fm.ascent(),
                    maxWidth,
                    totalHeight);
    textRect.adjust(-5, -5, 5, 5);

    // Build transformation matrix around center
    QPointF c = center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    t.translate(-c.x(), -c.y());

    // Transform corners
    QPolygonF poly;
    poly << t.map(textRect.topLeft())
         << t.map(textRect.topRight())
         << t.map(textRect.bottomRight())
         << t.map(textRect.bottomLeft());

    return poly;
}

bool TextAnnotation::containsPoint(const QPoint &point) const
{
    return transformedBoundingPolygon().containsPoint(QPointF(point), Qt::OddEvenFill);
}

bool TextAnnotation::isCacheValid(qreal dpr) const
{
    return !m_cachedPixmap.isNull() &&
           m_cachedDpr == dpr &&
           m_cachedText == m_text &&
           m_cachedFont == m_font &&
           m_cachedColor == m_color;
}

void TextAnnotation::regenerateCache(qreal dpr) const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    // Calculate bounds
    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // Create pixmap with padding for outline (3px on each side)
    int padding = 4;
    QSize pixmapSize(maxWidth + 2 * padding, totalHeight + 2 * padding);

    m_cachedPixmap = QPixmap(pixmapSize * dpr);
    m_cachedPixmap.setDevicePixelRatio(dpr);
    m_cachedPixmap.fill(Qt::transparent);

    {
        QPainter offPainter(&m_cachedPixmap);
        offPainter.setRenderHint(QPainter::TextAntialiasing, true);
        offPainter.setFont(m_font);

        QPoint pos(padding, padding + fm.ascent());

        for (const QString &line : lines) {
            if (!line.isEmpty()) {
                QPainterPath path;
                path.addText(pos, m_font, line);

                // White outline
                offPainter.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                offPainter.drawPath(path);

                // Fill with color
                offPainter.setPen(Qt::NoPen);
                offPainter.setBrush(m_color);
                offPainter.drawPath(path);
            }
            pos.setY(pos.y() + fm.lineSpacing());
        }
    }

    // Calculate origin: position is baseline, so offset by ascent and padding
    m_cachedOrigin = QPoint(m_position.x() - padding, m_position.y() - fm.ascent() - padding);

    // Update cache state
    m_cachedDpr = dpr;
    m_cachedText = m_text;
    m_cachedFont = m_font;
    m_cachedColor = m_color;
}

void TextAnnotation::draw(QPainter &painter) const
{
    if (m_text.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid, regenerate if needed
    if (!isCacheValid(dpr)) {
        regenerateCache(dpr);
    }

    painter.save();

    // Apply transformations around center
    QPointF c = center();
    painter.translate(c);
    painter.rotate(m_rotation);
    painter.scale(m_scale, m_scale);
    painter.translate(-c);

    // Draw cached pixmap
    painter.drawPixmap(m_cachedOrigin, m_cachedPixmap);

    painter.restore();
}

QRect TextAnnotation::boundingRect() const
{
    // Return axis-aligned bounding rect of transformed polygon
    return transformedBoundingPolygon().boundingRect().toAlignedRect();
}

std::unique_ptr<AnnotationItem> TextAnnotation::clone() const
{
    auto cloned = std::make_unique<TextAnnotation>(m_position, m_text, m_font, m_color);
    cloned->m_rotation = m_rotation;
    cloned->m_scale = m_scale;
    return cloned;
}

void TextAnnotation::setText(const QString &text)
{
    m_text = text;
    invalidateCache();
}

// ============================================================================
// StepBadgeAnnotation Implementation
// ============================================================================

StepBadgeAnnotation::StepBadgeAnnotation(const QPoint &position, const QColor &color, int number)
    : m_position(position)
    , m_color(color)
    , m_number(number)
{
}

void StepBadgeAnnotation::draw(QPainter &painter) const
{
    QTransform t = painter.transform();
    QPointF mappedPos = t.map(QPointF(m_position));
    qDebug() << "StepBadge::draw #" << m_number << "pos=" << m_position
             << "mapped=" << mappedPos
             << "device=" << painter.device()->width() << "x" << painter.device()->height();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw filled circle with annotation color
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawEllipse(m_position, kBadgeRadius, kBadgeRadius);

    // Draw white number in center
    painter.setPen(Qt::white);
    QFont font;
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    QRect textRect(m_position.x() - kBadgeRadius, m_position.y() - kBadgeRadius,
                   kBadgeRadius * 2, kBadgeRadius * 2);
    painter.drawText(textRect, Qt::AlignCenter, QString::number(m_number));

    painter.restore();
}

QRect StepBadgeAnnotation::boundingRect() const
{
    int margin = kBadgeRadius + 2;
    return QRect(m_position.x() - margin, m_position.y() - margin,
                 margin * 2, margin * 2);
}

std::unique_ptr<AnnotationItem> StepBadgeAnnotation::clone() const
{
    return std::make_unique<StepBadgeAnnotation>(m_position, m_color, m_number);
}

void StepBadgeAnnotation::setNumber(int number)
{
    m_number = number;
}

// ============================================================================
// MosaicStroke Implementation (freehand mosaic with enhanced privacy)
// ============================================================================

MosaicStroke::MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap,
                           int width, int blockSize, MosaicStrength strength)
    : m_points(points)
    , m_sourcePixmap(sourcePixmap)
    , m_width(width)
    , m_blockSize(blockSize)
    , m_strength(strength)
    , m_devicePixelRatio(sourcePixmap.devicePixelRatio())
{
}

void MosaicStroke::setStrength(MosaicStrength strength)
{
    if (m_strength != strength) {
        m_strength = strength;
        m_renderedCache = QPixmap();  // Invalidate cache
    }
}

QRgb MosaicStroke::calculateBlockAverageColor(const QImage &image, int x, int y,
                                               int blockW, int blockH, double jitter) const
{
    // Apply random jitter to sample region
    int offsetX = 0, offsetY = 0;
    if (jitter > 0) {
        int maxOffset = static_cast<int>(qMax(blockW, blockH) * jitter);
        if (maxOffset > 0) {
            offsetX = QRandomGenerator::global()->bounded(-maxOffset, maxOffset + 1);
            offsetY = QRandomGenerator::global()->bounded(-maxOffset, maxOffset + 1);
        }
    }

    // Calculate sampling bounds with jitter applied
    int startX = qBound(0, x + offsetX, image.width() - 1);
    int startY = qBound(0, y + offsetY, image.height() - 1);
    int endX = qBound(0, x + blockW + offsetX, image.width());
    int endY = qBound(0, y + blockH + offsetY, image.height());

    if (startX >= endX || startY >= endY) {
        // Fallback to original region if jitter pushed us out of bounds
        startX = x;
        startY = y;
        endX = qMin(x + blockW, image.width());
        endY = qMin(y + blockH, image.height());
    }

    // Calculate average color using scanLine for performance
    qint64 totalR = 0, totalG = 0, totalB = 0, totalA = 0;
    int pixelCount = 0;

    for (int py = startY; py < endY; ++py) {
        const QRgb *scanLine = reinterpret_cast<const QRgb*>(image.constScanLine(py));
        for (int px = startX; px < endX; ++px) {
            QRgb pixel = scanLine[px];
            totalR += qRed(pixel);
            totalG += qGreen(pixel);
            totalB += qBlue(pixel);
            totalA += qAlpha(pixel);
            ++pixelCount;
        }
    }

    if (pixelCount == 0) {
        return qRgba(0, 0, 0, 255);
    }

    return qRgba(
        static_cast<int>(totalR / pixelCount),
        static_cast<int>(totalG / pixelCount),
        static_cast<int>(totalB / pixelCount),
        static_cast<int>(totalA / pixelCount)
    );
}

QImage MosaicStroke::applyEnhancedMosaic(const QPixmap &regionPixmap) const
{
    QImage sourceImage = regionPixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    // Strength-based parameters
    double blockMultiplier;
    double jitter;
    int passes;

    switch (m_strength) {
        case MosaicStrength::Light:
            blockMultiplier = 0.5;
            jitter = 0.0;
            passes = 1;
            break;
        case MosaicStrength::Normal:
            blockMultiplier = 1.0;
            jitter = 0.1;
            passes = 1;
            break;
        case MosaicStrength::Strong:
            blockMultiplier = 1.5;
            jitter = 0.2;
            passes = 1;
            break;
        case MosaicStrength::Paranoid:
            blockMultiplier = 2.0;
            jitter = 0.3;
            passes = 2;
            break;
    }

    int effectiveBlockSize = qMax(4, static_cast<int>(m_blockSize * blockMultiplier * m_devicePixelRatio));

    for (int pass = 0; pass < passes; ++pass) {
        QImage resultImage(sourceImage.size(), QImage::Format_ARGB32);

        const int imgWidth = sourceImage.width();
        const int imgHeight = sourceImage.height();

        for (int blockY = 0; blockY < imgHeight; blockY += effectiveBlockSize) {
            for (int blockX = 0; blockX < imgWidth; blockX += effectiveBlockSize) {
                int blockW = qMin(effectiveBlockSize, imgWidth - blockX);
                int blockH = qMin(effectiveBlockSize, imgHeight - blockY);

                QRgb avgColor = calculateBlockAverageColor(sourceImage, blockX, blockY, blockW, blockH, jitter);

                // Fill block using scanLine for performance
                for (int py = blockY; py < blockY + blockH; ++py) {
                    QRgb *destLine = reinterpret_cast<QRgb*>(resultImage.scanLine(py));
                    std::fill(destLine + blockX, destLine + blockX + blockW, avgColor);
                }
            }
        }

        sourceImage = resultImage;  // Use result as source for next pass
    }

    return sourceImage;
}

void MosaicStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    QRect bounds = boundingRect();
    if (bounds.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid (including strength)
    bool cacheValid = !m_renderedCache.isNull()
        && m_cachedPointCount == m_points.size()
        && m_cachedBounds == bounds
        && m_cachedDpr == dpr
        && m_cachedStrength == m_strength;

    if (!cacheValid) {
        int halfWidth = m_width / 2;

        // === Step 1: Create pixelated version using enhanced algorithm ===
        // Convert bounds to device pixels
        QRect deviceBounds(
            static_cast<int>(bounds.x() * m_devicePixelRatio),
            static_cast<int>(bounds.y() * m_devicePixelRatio),
            static_cast<int>(bounds.width() * m_devicePixelRatio),
            static_cast<int>(bounds.height() * m_devicePixelRatio)
        );

        // Clamp to source pixmap bounds
        deviceBounds = deviceBounds.intersected(m_sourcePixmap.rect());
        if (deviceBounds.isEmpty()) {
            m_renderedCache = QPixmap();
            return;
        }

        // Extract region from source and apply enhanced mosaic
        QPixmap regionPixmap = m_sourcePixmap.copy(deviceBounds);
        QImage mosaicImage = applyEnhancedMosaic(regionPixmap);

        // === Step 2: Create mask from stroke path (square brush) ===
        QImage maskImage(bounds.size() * dpr, QImage::Format_Grayscale8);
        maskImage.fill(0);  // Start fully transparent

        QPainter maskPainter(&maskImage);
        maskPainter.setRenderHint(QPainter::Antialiasing, false);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(Qt::white);

        // Interpolation step for gap-free coverage
        int interpolationStep = qMax(1, halfWidth / 2);

        // Draw squares along the stroke path with interpolation
        for (int i = 0; i < m_points.size(); ++i) {
            QPoint pt = m_points[i];
            // Convert to mask coordinates (relative to bounds)
            int mx = static_cast<int>((pt.x() - bounds.left()) * dpr);
            int my = static_cast<int>((pt.y() - bounds.top()) * dpr);
            int size = static_cast<int>(m_width * dpr);

            maskPainter.fillRect(mx - size / 2, my - size / 2, size, size, Qt::white);

            // Interpolate to next point
            if (i < m_points.size() - 1) {
                QPoint nextPt = m_points[i + 1];
                int dx = nextPt.x() - pt.x();
                int dy = nextPt.y() - pt.y();
                int distSq = dx * dx + dy * dy;

                if (distSq > interpolationStep * interpolationStep) {
                    double dist = qSqrt(static_cast<double>(distSq));
                    int steps = static_cast<int>(dist / interpolationStep);

                    for (int s = 1; s < steps; ++s) {
                        double t = static_cast<double>(s) / steps;
                        int interpX = static_cast<int>((pt.x() + dx * t - bounds.left()) * dpr);
                        int interpY = static_cast<int>((pt.y() + dy * t - bounds.top()) * dpr);
                        maskPainter.fillRect(interpX - size / 2, interpY - size / 2, size, size, Qt::white);
                    }
                }
            }
        }
        maskPainter.end();

        // === Step 3: Composite mosaic image using mask ===
        // Apply mask to mosaic image
        for (int y = 0; y < qMin(mosaicImage.height(), maskImage.height()); ++y) {
            QRgb *pixelRow = reinterpret_cast<QRgb*>(mosaicImage.scanLine(y));
            const uchar *maskRow = maskImage.constScanLine(y);

            for (int x = 0; x < qMin(mosaicImage.width(), maskImage.width()); ++x) {
                int alpha = maskRow[x];  // 0-255 from grayscale mask
                if (alpha == 0) {
                    pixelRow[x] = qRgba(0, 0, 0, 0);  // Fully transparent
                } else {
                    // Keep original color, set alpha from mask
                    QRgb orig = pixelRow[x];
                    pixelRow[x] = qRgba(qRed(orig), qGreen(orig), qBlue(orig), alpha);
                }
            }
        }

        m_renderedCache = QPixmap::fromImage(mosaicImage);
        m_renderedCache.setDevicePixelRatio(dpr);

        m_cachedPointCount = m_points.size();
        m_cachedBounds = bounds;
        m_cachedDpr = dpr;
        m_cachedStrength = m_strength;
    }

    // Draw cached result
    painter.drawPixmap(bounds.topLeft(), m_renderedCache);
}

QRect MosaicStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    int minX = m_points[0].x();
    int maxX = m_points[0].x();
    int minY = m_points[0].y();
    int maxY = m_points[0].y();

    for (const QPoint &p : m_points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }

    int margin = m_width / 2 + m_blockSize;
    return QRect(minX - margin, minY - margin,
                 maxX - minX + 2 * margin, maxY - minY + 2 * margin);
}

std::unique_ptr<AnnotationItem> MosaicStroke::clone() const
{
    return std::make_unique<MosaicStroke>(m_points, m_sourcePixmap, m_width, m_blockSize, m_strength);
}

void MosaicStroke::addPoint(const QPoint &point)
{
    m_points.append(point);
}

void MosaicStroke::updateSource(const QPixmap &sourcePixmap)
{
    m_sourcePixmap = sourcePixmap;
    m_devicePixelRatio = sourcePixmap.devicePixelRatio();
    m_sourceImageCache = QImage();  // Clear cache so it regenerates on next draw
}

// ============================================================================
// ErasedItemsGroup Implementation
// ============================================================================

ErasedItemsGroup::ErasedItemsGroup(std::vector<IndexedItem> items)
    : m_erasedItems(std::move(items))
{
}

void ErasedItemsGroup::draw(QPainter &) const
{
    // ErasedItemsGroup is invisible - it's just a marker for undo support
}

QRect ErasedItemsGroup::boundingRect() const
{
    return QRect();  // Empty rect since this is just an undo marker
}

std::unique_ptr<AnnotationItem> ErasedItemsGroup::clone() const
{
    std::vector<IndexedItem> clonedItems;
    clonedItems.reserve(m_erasedItems.size());
    for (const auto &indexed : m_erasedItems) {
        clonedItems.push_back({indexed.originalIndex, indexed.item->clone()});
    }
    return std::make_unique<ErasedItemsGroup>(std::move(clonedItems));
}

std::vector<ErasedItemsGroup::IndexedItem> ErasedItemsGroup::extractItems()
{
    return std::move(m_erasedItems);
}

void ErasedItemsGroup::adjustIndicesForTrim(size_t trimCount)
{
    for (auto& indexed : m_erasedItems) {
        if (indexed.originalIndex >= trimCount) {
            indexed.originalIndex -= trimCount;
        } else {
            indexed.originalIndex = 0;
        }
    }
    for (size_t i = 0; i < m_originalIndices.size(); ++i) {
        if (m_originalIndices[i] >= trimCount) {
            m_originalIndices[i] -= trimCount;
        } else {
            m_originalIndices[i] = 0;
        }
    }
}

// ============================================================================
// AnnotationLayer Implementation
// ============================================================================

AnnotationLayer::AnnotationLayer(QObject *parent)
    : QObject(parent)
{
}

AnnotationLayer::~AnnotationLayer() = default;

void AnnotationLayer::addItem(std::unique_ptr<AnnotationItem> item)
{
    m_items.push_back(std::move(item));
    m_redoStack.clear();  // Clear redo stack when new item is added
    trimHistory();
    emit changed();
}

void AnnotationLayer::trimHistory()
{
    size_t trimCount = 0;
    while (m_items.size() > kMaxHistorySize) {
        m_items.erase(m_items.begin());
        ++trimCount;
    }
    if (trimCount > 0) {
        // Adjust stored indices in all ErasedItemsGroups
        for (auto& item : m_items) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        for (auto& item : m_redoStack) {
            if (auto* group = dynamic_cast<ErasedItemsGroup*>(item.get())) {
                group->adjustIndicesForTrim(trimCount);
            }
        }
        renumberStepBadges();
    }
}

void AnnotationLayer::undo()
{
    if (m_items.empty()) return;

    // Check if the last item is an ErasedItemsGroup (from eraser action)
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_items.back().get())) {
        // Extract the erased items with their original indices
        auto restoredItems = erasedGroup->extractItems();

        // Store original indices for redo support
        std::vector<size_t> indices;
        indices.reserve(restoredItems.size());
        for (const auto& item : restoredItems) {
            indices.push_back(item.originalIndex);
        }
        erasedGroup->setOriginalIndices(std::move(indices));

        // Move the empty group to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();

        // Sort by original index ascending to restore in correct order
        std::sort(restoredItems.begin(), restoredItems.end(),
            [](const auto& a, const auto& b) { return a.originalIndex < b.originalIndex; });

        // Re-insert items at their original indices
        for (auto &indexed : restoredItems) {
            size_t insertPos = std::min(indexed.originalIndex, m_items.size());
            m_items.insert(m_items.begin() + static_cast<ptrdiff_t>(insertPos), std::move(indexed.item));
        }
    } else {
        // Normal undo: move last item to redo stack
        m_redoStack.push_back(std::move(m_items.back()));
        m_items.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::redo()
{
    if (m_redoStack.empty()) return;

    // Check if the item in redo stack is an ErasedItemsGroup
    if (auto* erasedGroup = dynamic_cast<ErasedItemsGroup*>(m_redoStack.back().get())) {
        // Get the stored original indices
        auto indices = erasedGroup->originalIndices();
        m_redoStack.pop_back();

        // Sort indices in descending order to remove from back to front
        // (prevents index shifting issues during removal)
        std::sort(indices.begin(), indices.end(), std::greater<size_t>());

        // Remove items at the stored indices
        std::vector<ErasedItemsGroup::IndexedItem> itemsToErase;
        itemsToErase.reserve(indices.size());

        for (size_t idx : indices) {
            if (idx < m_items.size() && !dynamic_cast<ErasedItemsGroup*>(m_items[idx].get())) {
                itemsToErase.push_back({idx, std::move(m_items[idx])});
                m_items.erase(m_items.begin() + static_cast<ptrdiff_t>(idx));
            }
        }

        // Reverse to restore original order (we collected in descending index order)
        std::reverse(itemsToErase.begin(), itemsToErase.end());
        m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(itemsToErase)));
    } else {
        // Normal redo
        m_items.push_back(std::move(m_redoStack.back()));
        m_redoStack.pop_back();
    }

    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::clear()
{
    m_items.clear();
    m_redoStack.clear();
    emit changed();
}

void AnnotationLayer::draw(QPainter &painter) const
{
    for (const auto &item : m_items) {
        if (item->isVisible()) {
            item->draw(painter);
        }
    }
}

bool AnnotationLayer::canUndo() const
{
    return !m_items.empty();
}

bool AnnotationLayer::canRedo() const
{
    return !m_redoStack.empty();
}

bool AnnotationLayer::isEmpty() const
{
    return m_items.empty();
}

void AnnotationLayer::drawOntoPixmap(QPixmap &pixmap) const
{
    if (isEmpty()) return;

    QPainter painter(&pixmap);
    draw(painter);
}

int AnnotationLayer::countStepBadges() const
{
    int count = 0;
    for (const auto &item : m_items) {
        if (dynamic_cast<const StepBadgeAnnotation*>(item.get())) {
            ++count;
        }
    }
    return count;
}

void AnnotationLayer::renumberStepBadges()
{
    int badgeNumber = 1;
    for (auto &item : m_items) {
        if (auto* badge = dynamic_cast<StepBadgeAnnotation*>(item.get())) {
            badge->setNumber(badgeNumber++);
        }
    }
}

std::vector<ErasedItemsGroup::IndexedItem> AnnotationLayer::removeItemsIntersecting(const QPoint &point, int strokeWidth)
{
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    int radius = strokeWidth / 2;
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        // Skip ErasedItemsGroup items (they are invisible markers)
        if (dynamic_cast<ErasedItemsGroup*>(it->get())) {
            ++it;
            ++currentIndex;
            continue;
        }

        bool shouldRemove = false;

        // Use path-based intersection for strokes (more accurate)
        if (auto* pencil = dynamic_cast<PencilStroke*>(it->get())) {
            shouldRemove = pencil->intersectsCircle(point, radius);
        } else if (auto* marker = dynamic_cast<MarkerStroke*>(it->get())) {
            shouldRemove = marker->intersectsCircle(point, radius);
        } else {
            // Fallback: expanded bounding rect for shapes/text/badges/etc.
            QRect itemRect = (*it)->boundingRect();
            QRect expandedRect = itemRect.adjusted(-radius, -radius, radius, radius);
            shouldRemove = expandedRect.contains(point);
        }

        if (shouldRemove) {
            // Item intersects with eraser - remove it and record original index
            removedItems.push_back({currentIndex, std::move(*it)});
            it = m_items.erase(it);
            // Don't increment currentIndex since we erased
        } else {
            ++it;
            ++currentIndex;
        }
    }

    if (!removedItems.empty()) {
        m_redoStack.clear();  // Clear redo stack when items are erased
        renumberStepBadges();
        emit changed();
    }

    return removedItems;
}

int AnnotationLayer::hitTestText(const QPoint &pos) const
{
    // Iterate in reverse order (top-most items first)
    for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
        // Only hit-test TextAnnotation items
        if (auto* textItem = dynamic_cast<TextAnnotation*>(m_items[i].get())) {
            // Use containsPoint() for accurate hit-testing of rotated/scaled text
            if (textItem->containsPoint(pos)) {
                return i;
            }
        }
    }
    return -1;
}

AnnotationItem* AnnotationLayer::selectedItem()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size())) {
        return m_items[m_selectedIndex].get();
    }
    return nullptr;
}

AnnotationItem* AnnotationLayer::itemAt(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        return m_items[index].get();
    }
    return nullptr;
}

bool AnnotationLayer::removeSelectedItem()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_items.size())) {
        return false;
    }

    // Use ErasedItemsGroup for proper undo/redo support (same pattern as eraser)
    std::vector<ErasedItemsGroup::IndexedItem> removedItems;
    removedItems.push_back({static_cast<size_t>(m_selectedIndex), std::move(m_items[m_selectedIndex])});
    m_items.erase(m_items.begin() + m_selectedIndex);

    // Add ErasedItemsGroup to track the deletion for undo
    m_items.push_back(std::make_unique<ErasedItemsGroup>(std::move(removedItems)));

    m_redoStack.clear();
    m_selectedIndex = -1;
    renumberStepBadges();
    emit changed();
    return true;
}
