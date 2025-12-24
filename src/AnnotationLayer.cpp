#include "AnnotationLayer.h"
#include <QPainterPath>
#include <QtMath>
#include <QDebug>
#include <QSet>

// ============================================================================
// PencilStroke Implementation
// ============================================================================

PencilStroke::PencilStroke(const QVector<QPoint> &points, const QColor &color, int width)
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
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.drawPolyline(m_points.data(), m_points.size());

    painter.restore();
}

QRect PencilStroke::boundingRect() const
{
    if (m_points.isEmpty()) return QRect();

    if (m_boundingRectDirty) {
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

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(minX - margin, minY - margin,
                                    maxX - minX + 2 * margin, maxY - minY + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> PencilStroke::clone() const
{
    return std::make_unique<PencilStroke>(m_points, m_color, m_width);
}

void PencilStroke::addPoint(const QPoint &point)
{
    m_points.append(point);

    // Incrementally update bounding rect cache
    int margin = m_width / 2 + 1;
    QRect pointRect(point.x() - margin, point.y() - margin,
                    margin * 2, margin * 2);

    if (m_boundingRectDirty || m_boundingRectCache.isEmpty()) {
        m_boundingRectCache = pointRect;
        m_boundingRectDirty = false;
    } else {
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }
}

// ============================================================================
// MarkerStroke Implementation
// ============================================================================

MarkerStroke::MarkerStroke(const QVector<QPoint> &points, const QColor &color, int width)
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

            QPainterPath path;
            path.moveTo(m_points.first() - bounds.topLeft());
            for (int i = 1; i < m_points.size(); ++i) {
                path.lineTo(m_points[i] - bounds.topLeft());
            }
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

        int margin = m_width / 2 + 1;
        m_boundingRectCache = QRect(minX - margin, minY - margin,
                                    maxX - minX + 2 * margin, maxY - minY + 2 * margin);
        m_boundingRectDirty = false;
    }
    return m_boundingRectCache;
}

std::unique_ptr<AnnotationItem> MarkerStroke::clone() const
{
    return std::make_unique<MarkerStroke>(m_points, m_color, m_width);
}

void MarkerStroke::addPoint(const QPoint &point)
{
    m_points.append(point);

    // Incrementally update bounding rect cache
    int margin = m_width / 2 + 1;
    QRect pointRect(point.x() - margin, point.y() - margin,
                    margin * 2, margin * 2);

    if (m_boundingRectDirty || m_boundingRectCache.isEmpty()) {
        m_boundingRectCache = pointRect;
        m_boundingRectDirty = false;
    } else {
        m_boundingRectCache = m_boundingRectCache.united(pointRect);
    }
}

// ============================================================================
// ArrowAnnotation Implementation
// ============================================================================

ArrowAnnotation::ArrowAnnotation(const QPoint &start, const QPoint &end, const QColor &color, int width)
    : m_start(start)
    , m_end(end)
    , m_color(color)
    , m_width(width)
{
}

void ArrowAnnotation::draw(QPainter &painter) const
{
    painter.save();
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw the line
    painter.drawLine(m_start, m_end);

    // Draw the arrowhead
    drawArrowhead(painter, m_start, m_end);

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
    return std::make_unique<ArrowAnnotation>(m_start, m_end, m_color, m_width);
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
        QColor fillColor = m_color;
        fillColor.setAlpha(50);
        painter.setBrush(fillColor);
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
        QColor fillColor = m_color;
        fillColor.setAlpha(50);
        painter.setBrush(fillColor);
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

void TextAnnotation::draw(QPainter &painter) const
{
    if (m_text.isEmpty()) return;

    painter.save();
    painter.setFont(m_font);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Split text into lines for multi-line support
    QStringList lines = m_text.split('\n');
    QFontMetrics fm(m_font);
    int lineHeight = fm.lineSpacing();
    QPoint currentPos = m_position;

    for (const QString &line : lines) {
        if (!line.isEmpty()) {
            QPainterPath path;
            path.addText(currentPos, m_font, line);

            // White outline
            QPen outlinePen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(outlinePen);
            painter.drawPath(path);

            // Fill with color
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_color);
            painter.drawPath(path);
        }
        currentPos.setY(currentPos.y() + lineHeight);
    }

    painter.restore();
}

QRect TextAnnotation::boundingRect() const
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }
    int totalHeight = lines.count() * fm.lineSpacing();

    // m_position is baseline position of first line
    QRect textRect(m_position.x(),
                   m_position.y() - fm.ascent(),
                   maxWidth,
                   totalHeight);
    return textRect.adjusted(-5, -5, 5, 5);  // Add margin for outline
}

std::unique_ptr<AnnotationItem> TextAnnotation::clone() const
{
    return std::make_unique<TextAnnotation>(m_position, m_text, m_font, m_color);
}

void TextAnnotation::setText(const QString &text)
{
    m_text = text;
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
// MosaicAnnotation Implementation
// ============================================================================

MosaicAnnotation::MosaicAnnotation(const QRect &rect, const QPixmap &sourcePixmap, int blockSize)
    : m_rect(rect)
    , m_sourcePixmap(sourcePixmap)
    , m_blockSize(blockSize)
    , m_devicePixelRatio(sourcePixmap.devicePixelRatio())
{
    generateMosaic();
}

void MosaicAnnotation::draw(QPainter &painter) const
{
    if (m_mosaicPixmap.isNull()) return;

    painter.save();
    QRect normalizedRect = m_rect.normalized();
    painter.drawPixmap(normalizedRect, m_mosaicPixmap);
    painter.restore();
}

QRect MosaicAnnotation::boundingRect() const
{
    return m_rect.normalized();
}

std::unique_ptr<AnnotationItem> MosaicAnnotation::clone() const
{
    auto cloned = std::make_unique<MosaicAnnotation>(m_rect, m_sourcePixmap, m_blockSize);
    return cloned;
}

void MosaicAnnotation::setRect(const QRect &rect)
{
    m_rect = rect;
    generateMosaic();
}

void MosaicAnnotation::updateSource(const QPixmap &sourcePixmap)
{
    m_sourcePixmap = sourcePixmap;
    m_devicePixelRatio = sourcePixmap.devicePixelRatio();
    generateMosaic();
}

void MosaicAnnotation::generateMosaic()
{
    QRect normalizedRect = m_rect.normalized();
    if (normalizedRect.isEmpty() || m_sourcePixmap.isNull()) {
        m_mosaicPixmap = QPixmap();
        return;
    }

    // Convert logical coordinates to device pixel coordinates for HiDPI displays
    QRect deviceRect(
        static_cast<int>(normalizedRect.x() * m_devicePixelRatio),
        static_cast<int>(normalizedRect.y() * m_devicePixelRatio),
        static_cast<int>(normalizedRect.width() * m_devicePixelRatio),
        static_cast<int>(normalizedRect.height() * m_devicePixelRatio)
    );

    // Extract the region from source using device pixel coordinates
    QRect sourceRect = deviceRect.intersected(m_sourcePixmap.rect());
    if (sourceRect.isEmpty()) {
        m_mosaicPixmap = QPixmap();
        return;
    }

    QImage sourceImage = m_sourcePixmap.copy(sourceRect).toImage();
    QImage mosaicImage(sourceImage.size(), QImage::Format_ARGB32);

    // Scale block size for device pixels
    int deviceBlockSize = static_cast<int>(m_blockSize * m_devicePixelRatio);
    if (deviceBlockSize < 1) deviceBlockSize = 1;

    // Generate mosaic by sampling blocks
    for (int y = 0; y < sourceImage.height(); y += deviceBlockSize) {
        for (int x = 0; x < sourceImage.width(); x += deviceBlockSize) {
            // Get average color of block (sample center)
            int sampleX = qMin(x + deviceBlockSize / 2, sourceImage.width() - 1);
            int sampleY = qMin(y + deviceBlockSize / 2, sourceImage.height() - 1);
            QColor blockColor = sourceImage.pixelColor(sampleX, sampleY);

            // Fill the block with sampled color
            for (int by = y; by < qMin(y + deviceBlockSize, sourceImage.height()); ++by) {
                for (int bx = x; bx < qMin(x + deviceBlockSize, sourceImage.width()); ++bx) {
                    mosaicImage.setPixelColor(bx, by, blockColor);
                }
            }
        }
    }

    m_mosaicPixmap = QPixmap::fromImage(mosaicImage);
    m_mosaicPixmap.setDevicePixelRatio(m_devicePixelRatio);
}

// ============================================================================
// MosaicStroke Implementation (freehand mosaic)
// ============================================================================

MosaicStroke::MosaicStroke(const QVector<QPoint> &points, const QPixmap &sourcePixmap, int width, int blockSize)
    : m_points(points)
    , m_sourcePixmap(sourcePixmap)
    , m_width(width)
    , m_blockSize(blockSize)
    , m_devicePixelRatio(sourcePixmap.devicePixelRatio())
{
}

void MosaicStroke::draw(QPainter &painter) const
{
    if (m_points.size() < 2) return;

    QRect bounds = boundingRect();
    if (bounds.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid
    bool cacheValid = !m_renderedCache.isNull()
        && m_cachedPointCount == m_points.size()
        && m_cachedBounds == bounds
        && m_cachedDpr == dpr;

    if (!cacheValid) {
        int halfWidth = m_width / 2;

        // === Step 1: Create pixelated version of the bounded region ===
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

        // Extract region from source
        QPixmap regionPixmap = m_sourcePixmap.copy(deviceBounds);

        // Pixelate: downscale then upscale
        int pixelateBlockSize = qMax(4, m_blockSize * 2);  // Larger blocks for stronger effect
        QSize smallSize(
            qMax(1, deviceBounds.width() / pixelateBlockSize),
            qMax(1, deviceBounds.height() / pixelateBlockSize)
        );

        // Downscale (average colors)
        QPixmap downscaled = regionPixmap.scaled(smallSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Upscale back (creates blocky pixelated look)
        QPixmap pixelated = downscaled.scaled(deviceBounds.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

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

        // === Step 3: Composite pixelated image using mask ===
        QImage pixelatedImage = pixelated.toImage().convertToFormat(QImage::Format_ARGB32);

        // Apply mask to pixelated image
        for (int y = 0; y < qMin(pixelatedImage.height(), maskImage.height()); ++y) {
            QRgb *pixelRow = reinterpret_cast<QRgb*>(pixelatedImage.scanLine(y));
            const uchar *maskRow = maskImage.constScanLine(y);

            for (int x = 0; x < qMin(pixelatedImage.width(), maskImage.width()); ++x) {
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

        m_renderedCache = QPixmap::fromImage(pixelatedImage);
        m_renderedCache.setDevicePixelRatio(dpr);

        m_cachedPointCount = m_points.size();
        m_cachedBounds = bounds;
        m_cachedDpr = dpr;
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
    return std::make_unique<MosaicStroke>(m_points, m_sourcePixmap, m_width, m_blockSize);
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
        item->draw(painter);
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
    int margin = strokeWidth / 2;
    size_t currentIndex = 0;

    for (auto it = m_items.begin(); it != m_items.end(); ) {
        // Skip ErasedItemsGroup items (they are invisible markers)
        if (dynamic_cast<ErasedItemsGroup*>(it->get())) {
            ++it;
            ++currentIndex;
            continue;
        }

        QRect itemRect = (*it)->boundingRect();
        QRect expandedRect = itemRect.adjusted(-margin, -margin, margin, margin);

        if (expandedRect.contains(point)) {
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
        if (dynamic_cast<TextAnnotation*>(m_items[i].get())) {
            if (m_items[i]->boundingRect().contains(pos)) {
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
