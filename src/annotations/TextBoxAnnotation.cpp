#include "annotations/TextBoxAnnotation.h"
#include "utils/CoordinateHelper.h"
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

QTransform TextBoxAnnotation::localLinearTransform() const
{
    QTransform t;

    if (!qFuzzyIsNull(m_rotation)) {
        t.rotate(m_rotation);
    }

    if (m_mirrorX || m_mirrorY) {
        t.scale(m_mirrorX ? -1.0 : 1.0, m_mirrorY ? -1.0 : 1.0);
    }

    if (!qFuzzyCompare(m_scale, 1.0)) {
        t.scale(m_scale, m_scale);
    }

    return t;
}

QPolygonF TextBoxAnnotation::transformedBoundingPolygon() const
{
    // Build untransformed bounding rect with margin for easier click detection
    QRectF worldBox(m_position.x(), m_position.y(), m_box.width(), m_box.height());
    worldBox.adjust(-kHitMargin, -kHitMargin, kHitMargin, kHitMargin);

    const QPointF c = center();
    const QTransform linear = localLinearTransform();

    auto mapAroundCenter = [&c, &linear](const QPointF& point) {
        return c + linear.map(point - c);
    };

    // Transform corners
    QPolygonF poly;
    poly << mapAroundCenter(worldBox.topLeft())
         << mapAroundCenter(worldBox.topRight())
         << mapAroundCenter(worldBox.bottomRight())
         << mapAroundCenter(worldBox.bottomLeft());

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
    m_cachedPixmap = QPixmap(CoordinateHelper::toPhysical(pixmapSize, dpr));
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

    // Apply rotation, mirror and scale around center
    QPointF c = center();
    painter.translate(c);
    painter.setTransform(localLinearTransform(), true);
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
    cloned->m_mirrorX = m_mirrorX;
    cloned->m_mirrorY = m_mirrorY;
    return cloned;
}

void TextBoxAnnotation::setText(const QString &text)
{
    m_text = text;
    // Recalculate box size to fit new text
    calculateInitialBox();
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

void TextBoxAnnotation::translate(const QPointF& delta)
{
    moveBy(delta);
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

void TextBoxAnnotation::setMirror(bool mirrorX, bool mirrorY)
{
    m_mirrorX = mirrorX;
    m_mirrorY = mirrorY;
    invalidateCache();
}

QPointF TextBoxAnnotation::mapLocalPointToTransformed(const QPointF& localPoint) const
{
    QPointF halfSize(m_box.width() / 2.0, m_box.height() / 2.0);
    QPointF transformedOffset = localLinearTransform().map(localPoint - halfSize);
    return m_position + halfSize + transformedOffset;
}

QPointF TextBoxAnnotation::topLeftFromTransformedLocalPoint(const QPointF& transformedPoint,
                                                            const QPointF& localPoint) const
{
    QPointF halfSize(m_box.width() / 2.0, m_box.height() / 2.0);
    QPointF transformedOffset = localLinearTransform().map(localPoint - halfSize);
    return transformedPoint - halfSize - transformedOffset;
}
