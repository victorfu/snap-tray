#include "annotations/EmojiStickerAnnotation.h"
#include <QPainter>
#include <QFontMetrics>
#include <QTransform>
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

QPolygonF EmojiStickerAnnotation::transformedBoundingPolygon() const
{
    // Use actual font metrics to match the draw() calculation
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

    // Match the draw() positioning exactly
    int x = m_position.x() - textRect.width() / 2;
    int y = m_position.y() + fm.ascent() / 2 - fm.ascent();

    QRectF rect(x, y, textRect.width(), fm.height());

    QPolygonF poly;
    poly << rect.topLeft()
         << rect.topRight()
         << rect.bottomRight()
         << rect.bottomLeft();

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

    // Counter-rotate to keep emoji upright regardless of canvas rotation
    QTransform worldTransform = painter.worldTransform();
    qreal rotationAngle = qRadiansToDegrees(qAtan2(worldTransform.m12(), worldTransform.m11()));

    painter.translate(m_position);
    painter.rotate(-rotationAngle);
    painter.translate(-m_position);

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

    int x = m_position.x() - textRect.width() / 2;
    int y = m_position.y() + fm.ascent() / 2;

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
    return std::make_unique<EmojiStickerAnnotation>(m_position, m_emoji, m_scale);
}
