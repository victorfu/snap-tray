#include "annotations/TextBoxAnnotation.h"
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QTransform>

TextBoxAnnotation::TextBoxAnnotation(const QPointF &position, const QString &text,
                                     const QFont &font, const QColor &color)
    : m_position(position)
    , m_text(text)
    , m_font(font)
    , m_color(color)
{
    calculateInitialBox();
}

void TextBoxAnnotation::calculateInitialBox()
{
    QFontMetrics fm(m_font);
    QStringList lines = m_text.split('\n');

    // Calculate natural text width (no wrap)
    int maxWidth = 0;
    for (const QString &line : lines) {
        maxWidth = qMax(maxWidth, fm.horizontalAdvance(line));
    }

    // Use either default width or natural width, whichever is larger
    int boxWidth = qMax(kDefaultWidth, maxWidth + 2 * kPadding);

    // Calculate height based on wrapped text with this width
    m_box = QRectF(0, 0, boxWidth, fm.lineSpacing() + 2 * kPadding);

    // Now wrap text and recalculate height
    QStringList wrapped = wrapText();
    int totalHeight = wrapped.count() * fm.lineSpacing() + 2 * kPadding;
    m_box.setHeight(qMax(totalHeight, kMinHeight));
}

QStringList TextBoxAnnotation::wrapText() const
{
    QFontMetrics fm(m_font);
    int maxWidth = static_cast<int>(m_box.width()) - 2 * kPadding;

    if (maxWidth <= 0) {
        return m_text.split('\n');
    }

    QStringList result;
    QStringList paragraphs = m_text.split('\n');

    for (const QString &para : paragraphs) {
        if (para.isEmpty()) {
            result.append(QString());
            continue;
        }

        QString currentLine;
        QStringList words = para.split(' ');

        for (const QString &word : words) {
            if (word.isEmpty()) continue;

            QString testLine = currentLine.isEmpty() ? word : currentLine + ' ' + word;

            if (fm.horizontalAdvance(testLine) <= maxWidth) {
                currentLine = testLine;
            } else {
                if (!currentLine.isEmpty()) {
                    result.append(currentLine);
                }
                // If single word is too long, add it anyway (will be clipped)
                currentLine = word;
            }
        }

        if (!currentLine.isEmpty()) {
            result.append(currentLine);
        }
    }

    // Ensure at least one line
    if (result.isEmpty()) {
        result.append(QString());
    }

    return result;
}

QPointF TextBoxAnnotation::center() const
{
    // Center of the box in world coordinates
    return m_position + QPointF(m_box.width() / 2.0, m_box.height() / 2.0);
}

QPolygonF TextBoxAnnotation::transformedBoundingPolygon() const
{
    // Build untransformed bounding rect with margin for easier click detection
    QRectF worldBox(m_position.x(), m_position.y(), m_box.width(), m_box.height());
    worldBox.adjust(-kHitMargin, -kHitMargin, kHitMargin, kHitMargin);

    // Build transformation matrix around center (rotate + scale)
    QPointF c = center();
    QTransform t;
    t.translate(c.x(), c.y());
    t.rotate(m_rotation);
    t.scale(m_scale, m_scale);
    t.translate(-c.x(), -c.y());

    // Transform corners
    QPolygonF poly;
    poly << t.map(worldBox.topLeft())
         << t.map(worldBox.topRight())
         << t.map(worldBox.bottomRight())
         << t.map(worldBox.bottomLeft());

    return poly;
}

bool TextBoxAnnotation::containsPoint(const QPoint &point) const
{
    return transformedBoundingPolygon().containsPoint(QPointF(point), Qt::OddEvenFill);
}

bool TextBoxAnnotation::isCacheValid(qreal dpr) const
{
    return !m_cachedPixmap.isNull() &&
           m_cachedDpr == dpr &&
           m_cachedText == m_text &&
           m_cachedFont == m_font &&
           m_cachedColor == m_color &&
           m_cachedBox == m_box;
}

void TextBoxAnnotation::regenerateCache(qreal dpr) const
{
    QFontMetrics fm(m_font);
    QStringList lines = wrapText();

    int boxWidth = static_cast<int>(m_box.width());
    int boxHeight = static_cast<int>(m_box.height());

    // Create pixmap for the box
    QSize pixmapSize(boxWidth, boxHeight);
    m_cachedPixmap = QPixmap(pixmapSize * dpr);
    m_cachedPixmap.setDevicePixelRatio(dpr);
    m_cachedPixmap.fill(Qt::transparent);

    {
        QPainter offPainter(&m_cachedPixmap);
        offPainter.setRenderHint(QPainter::TextAntialiasing, true);
        offPainter.setFont(m_font);

        // Start at top-left with padding, then add ascent for baseline
        QPointF pos(kPadding, kPadding + fm.ascent());

        for (const QString &line : lines) {
            if (!line.isEmpty()) {
                QPainterPath path;
                path.addText(pos, m_font, line);

                // Fill with color
                offPainter.setPen(Qt::NoPen);
                offPainter.setBrush(m_color);
                offPainter.drawPath(path);
            }
            pos.setY(pos.y() + fm.lineSpacing());
        }
    }

    // Origin is the position (top-left of box)
    m_cachedOrigin = m_position;

    // Update cache state
    m_cachedDpr = dpr;
    m_cachedText = m_text;
    m_cachedFont = m_font;
    m_cachedColor = m_color;
    m_cachedBox = m_box;
}

void TextBoxAnnotation::draw(QPainter &painter) const
{
    if (m_text.isEmpty()) return;

    qreal dpr = painter.device()->devicePixelRatio();

    // Check if cache is valid, regenerate if needed
    if (!isCacheValid(dpr)) {
        regenerateCache(dpr);
    }

    painter.save();

    // Apply rotation and scale around center
    QPointF c = center();
    painter.translate(c);
    painter.rotate(m_rotation);
    painter.scale(m_scale, m_scale);
    painter.translate(-c);

    // Draw cached pixmap at position
    painter.drawPixmap(m_cachedOrigin, m_cachedPixmap);

    painter.restore();
}

QRect TextBoxAnnotation::boundingRect() const
{
    return transformedBoundingPolygon().boundingRect().toAlignedRect();
}

std::unique_ptr<AnnotationItem> TextBoxAnnotation::clone() const
{
    auto cloned = std::make_unique<TextBoxAnnotation>(m_position, m_text, m_font, m_color);
    cloned->m_box = m_box;
    cloned->m_rotation = m_rotation;
    cloned->m_scale = m_scale;
    return cloned;
}

void TextBoxAnnotation::setText(const QString &text)
{
    m_text = text;
    // Recalculate box size to fit new text
    calculateInitialBox();
    invalidateCache();
}

void TextBoxAnnotation::setBox(const QRectF &box)
{
    // Enforce minimum size
    m_box.setWidth(qMax(box.width(), static_cast<qreal>(kMinWidth)));
    m_box.setHeight(qMax(box.height(), static_cast<qreal>(kMinHeight)));
    invalidateCache();
}

void TextBoxAnnotation::setPosition(const QPointF &position)
{
    m_position = position;
    invalidateCache();
}

void TextBoxAnnotation::moveBy(const QPointF &delta)
{
    m_position += delta;
    invalidateCache();
}

void TextBoxAnnotation::setFont(const QFont &font)
{
    m_font = font;
    // Recalculate box size since font affects text dimensions
    calculateInitialBox();
    invalidateCache();
}

void TextBoxAnnotation::setColor(const QColor &color)
{
    m_color = color;
    invalidateCache();
}

void TextBoxAnnotation::setRotation(qreal degrees)
{
    m_rotation = degrees;
    invalidateCache();
}

void TextBoxAnnotation::setScale(qreal scale)
{
    m_scale = scale;
    invalidateCache();
}
