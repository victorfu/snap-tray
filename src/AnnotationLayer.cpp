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
    QPen pen(m_color, m_width, Qt::SolidLine, Qt::FlatCap, Qt::BevelJoin);
    painter.setPen(pen);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.drawPolyline(m_points.data(), m_points.size());

    painter.restore();
}

QRect PencilStroke::boundingRect() const
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

    int margin = m_width / 2 + 1;
    return QRect(minX - margin, minY - margin,
                 maxX - minX + 2 * margin, maxY - minY + 2 * margin);
}

std::unique_ptr<AnnotationItem> PencilStroke::clone() const
{
    return std::make_unique<PencilStroke>(m_points, m_color, m_width);
}

void PencilStroke::addPoint(const QPoint &point)
{
    m_points.append(point);
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

    // Calculate bounding rect for offscreen buffer
    QRect bounds = boundingRect();
    if (bounds.isEmpty()) {
        painter.restore();
        return;
    }

    // Create offscreen pixmap with transparent background
    // This ensures uniform transparency even when stroke overlaps itself
    QPixmap offscreen(bounds.size());
    offscreen.fill(Qt::transparent);

    {
        QPainter offPainter(&offscreen);
        offPainter.setRenderHint(QPainter::Antialiasing, true);

        // Draw with FULL opacity on the offscreen buffer
        QPen pen(m_color, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        offPainter.setPen(pen);
        offPainter.setBrush(Qt::NoBrush);

        // Translate points relative to offscreen origin
        QPainterPath path;
        path.moveTo(m_points.first() - bounds.topLeft());
        for (int i = 1; i < m_points.size(); ++i) {
            path.lineTo(m_points[i] - bounds.topLeft());
        }
        offPainter.drawPath(path);
    }

    // Composite the offscreen pixmap with desired alpha (~40% opacity)
    painter.setOpacity(0.4);
    painter.drawPixmap(bounds.topLeft(), offscreen);

    painter.restore();
}

QRect MarkerStroke::boundingRect() const
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

    int margin = m_width / 2 + 1;
    return QRect(minX - margin, minY - margin,
                 maxX - minX + 2 * margin, maxY - minY + 2 * margin);
}

std::unique_ptr<AnnotationItem> MarkerStroke::clone() const
{
    return std::make_unique<MarkerStroke>(m_points, m_color, m_width);
}

void MarkerStroke::addPoint(const QPoint &point)
{
    m_points.append(point);
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

    painter.drawRect(m_rect.normalized());

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

    // Draw text with outline for better visibility
    QPainterPath path;
    path.addText(m_position, m_font, m_text);

    // White outline
    QPen outlinePen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(outlinePen);
    painter.drawPath(path);

    // Fill with color
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawPath(path);

    painter.restore();
}

QRect TextAnnotation::boundingRect() const
{
    QFontMetrics fm(m_font);
    QRect textRect = fm.boundingRect(m_text);
    textRect.translate(m_position);  // m_position is baseline position, preserve relative coords
    return textRect.adjusted(-5, -5, 5, 5);  // Add margin
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

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    int halfWidth = m_width / 2;
    int deviceBlockSize = qMax(1, static_cast<int>(m_blockSize * m_devicePixelRatio));

    // OPTIMIZATION: Cache QImage conversion (only convert once per MosaicStroke lifetime)
    if (m_sourceImageCache.isNull()) {
        m_sourceImageCache = m_sourcePixmap.toImage();
    }
    const QImage &sourceImage = m_sourceImageCache;

    // OPTIMIZATION: Track processed blocks to avoid overdraw
    QSet<QPair<int, int>> processedBlocks;

    int devRadius = static_cast<int>(halfWidth * m_devicePixelRatio);
    int radiusSq = devRadius * devRadius;

    for (const QPoint &pt : m_points) {
        int devPtX = static_cast<int>(pt.x() * m_devicePixelRatio);
        int devPtY = static_cast<int>(pt.y() * m_devicePixelRatio);

        // Calculate block range for this point (block-aligned iteration)
        int blockLeft = qMax(0, (devPtX - devRadius) / deviceBlockSize);
        int blockTop = qMax(0, (devPtY - devRadius) / deviceBlockSize);
        int blockRight = (devPtX + devRadius) / deviceBlockSize;
        int blockBottom = (devPtY + devRadius) / deviceBlockSize;

        for (int by = blockTop; by <= blockBottom; ++by) {
            for (int bx = blockLeft; bx <= blockRight; ++bx) {
                // Skip if already processed
                auto blockKey = qMakePair(bx, by);
                if (processedBlocks.contains(blockKey)) continue;

                int dx = bx * deviceBlockSize;
                int dy = by * deviceBlockSize;

                // Bounds check
                if (dx >= m_sourcePixmap.width() || dy >= m_sourcePixmap.height()) continue;

                // Circular brush check
                int blockCenterX = dx + deviceBlockSize / 2;
                int blockCenterY = dy + deviceBlockSize / 2;
                int distSq = (blockCenterX - devPtX) * (blockCenterX - devPtX) +
                             (blockCenterY - devPtY) * (blockCenterY - devPtY);
                if (distSq > radiusSq) continue;

                // Mark as processed
                processedBlocks.insert(blockKey);

                // Sample center of block
                int sampleX = qMin(dx + deviceBlockSize / 2, m_sourcePixmap.width() - 1);
                int sampleY = qMin(dy + deviceBlockSize / 2, m_sourcePixmap.height() - 1);
                QColor blockColor = sourceImage.pixelColor(sampleX, sampleY);

                // Blend with gray for desaturated/muted look
                int gray = (blockColor.red() + blockColor.green() + blockColor.blue()) / 3;
                blockColor.setRed((blockColor.red() + gray) / 2);
                blockColor.setGreen((blockColor.green() + gray) / 2);
                blockColor.setBlue((blockColor.blue() + gray) / 2);

                // Calculate logical rect for this block
                QRect logicalRect(
                    static_cast<int>(dx / m_devicePixelRatio),
                    static_cast<int>(dy / m_devicePixelRatio),
                    qMax(1, static_cast<int>(deviceBlockSize / m_devicePixelRatio)),
                    qMax(1, static_cast<int>(deviceBlockSize / m_devicePixelRatio))
                );

                painter.fillRect(logicalRect, blockColor);
            }
        }
    }

    painter.restore();
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
    bool hadDeletions = false;
    while (m_items.size() > kMaxHistorySize) {
        m_items.erase(m_items.begin());
        hadDeletions = true;
    }
    if (hadDeletions) {
        renumberStepBadges();
    }
}

void AnnotationLayer::undo()
{
    if (m_items.empty()) return;

    m_redoStack.push_back(std::move(m_items.back()));
    m_items.pop_back();
    renumberStepBadges();
    emit changed();
}

void AnnotationLayer::redo()
{
    if (m_redoStack.empty()) return;

    m_items.push_back(std::move(m_redoStack.back()));
    m_redoStack.pop_back();
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
