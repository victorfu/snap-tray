#include "annotations/EmojiStickerAnnotation.h"
#include <QPainter>
#include <QFontMetrics>
#include <cmath>
#include <QtMath>

EmojiStickerAnnotation::EmojiStickerAnnotation(const QPoint &position, const QString &emoji, qreal scale)
    : m_position(position)
    , m_emoji(emoji)
    , m_scale(scale)
{
}

void EmojiStickerAnnotation::setScale(qreal scale)
{
    m_scale = qBound(kMinScale, scale, kMaxScale);
}

void EmojiStickerAnnotation::setRotation(qreal degrees)
{
    qreal normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    if (qFuzzyIsNull(normalized)) {
        normalized = 0.0;
    }
    m_rotation = normalized;
}

void EmojiStickerAnnotation::setMirror(bool mirrorX, bool mirrorY)
{
    m_mirrorX = mirrorX;
    m_mirrorY = mirrorY;
}

QSize EmojiStickerAnnotation::emojiSize() const
{
    int scaledSize = static_cast<int>(kBaseSize * m_scale);
    return QSize(scaledSize, scaledSize);
}

QPointF EmojiStickerAnnotation::center() const
{
    // Return center of the actual bounding polygon
    QPolygonF poly = transformedBoundingPolygon();
    QRectF bounds = poly.boundingRect();
    return bounds.center();
}

QRectF EmojiStickerAnnotation::glyphRect() const
{
    int fontSize = static_cast<int>(kBaseSize * m_scale);
    QFont font;
#ifdef Q_OS_MAC
    font.setFamily("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    font.setFamily("Segoe UI Emoji");
#else
    font.setFamily("Noto Color Emoji");
#endif
    font.setPointSize(fontSize);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(m_emoji);

    qreal baselineX = m_position.x() - textRect.width() / 2.0;
    qreal baselineY = m_position.y() + fm.ascent() / 2.0;
    qreal top = baselineY - fm.ascent();

    return QRectF(baselineX, top, textRect.width(), fm.height());
}

QTransform EmojiStickerAnnotation::localLinearTransform() const
{
    QTransform t;
    if (!qFuzzyIsNull(m_rotation)) {
        t.rotate(m_rotation);
    }
    if (m_mirrorX || m_mirrorY) {
        t.scale(m_mirrorX ? -1.0 : 1.0, m_mirrorY ? -1.0 : 1.0);
    }
    return t;
}

QPolygonF EmojiStickerAnnotation::transformedBoundingPolygon() const
{
    QRectF rect = glyphRect();

    QPolygonF poly;
    poly << rect.topLeft()
         << rect.topRight()
         << rect.bottomRight()
         << rect.bottomLeft();

    const QPointF c = rect.center();
    const QTransform linear = localLinearTransform();
    for (int i = 0; i < poly.size(); ++i) {
        poly[i] = c + linear.map(poly[i] - c);
    }

    return poly;
}

bool EmojiStickerAnnotation::containsPoint(const QPoint &point) const
{
    return transformedBoundingPolygon().containsPoint(QPointF(point), Qt::OddEvenFill);
}

void EmojiStickerAnnotation::draw(QPainter &painter) const
{
    if (m_emoji.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    int fontSize = static_cast<int>(kBaseSize * m_scale);
    QFont font;
#ifdef Q_OS_MAC
    font.setFamily("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    font.setFamily("Segoe UI Emoji");
#else
    font.setFamily("Noto Color Emoji");
#endif
    font.setPointSize(fontSize);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(m_emoji);

    qreal x = m_position.x() - textRect.width() / 2.0;
    qreal y = m_position.y() + fm.ascent() / 2.0;

    QRectF rect = glyphRect();
    QPointF c = rect.center();
    painter.translate(c);
    painter.setTransform(localLinearTransform(), true);
    painter.translate(-c);

    painter.drawText(x, y, m_emoji);

    painter.restore();
}

QRect EmojiStickerAnnotation::boundingRect() const
{
    QPolygonF poly = transformedBoundingPolygon();
    QRect rect = poly.boundingRect().toRect();
    // Add margin for selection handles
    int margin = 4;
    return rect.adjusted(-margin, -margin, margin, margin);
}

std::unique_ptr<AnnotationItem> EmojiStickerAnnotation::clone() const
{
    auto cloned = std::make_unique<EmojiStickerAnnotation>(m_position, m_emoji, m_scale);
    cloned->setRotation(m_rotation);
    cloned->setMirror(m_mirrorX, m_mirrorY);
    return cloned;
}
