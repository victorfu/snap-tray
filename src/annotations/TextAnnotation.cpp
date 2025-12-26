#include "annotations/TextAnnotation.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QTransform>

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
           m_cachedColor == m_color &&
           m_cachedPosition == m_position;
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
    m_cachedPosition = m_position;
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
