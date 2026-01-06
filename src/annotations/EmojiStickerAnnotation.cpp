#include "annotations/EmojiStickerAnnotation.h"
#include <QPainter>
#include <QFontMetrics>
#include <QTransform>

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
    return QPointF(m_position);
}

QPolygonF EmojiStickerAnnotation::transformedBoundingPolygon() const
{
    QSize size = emojiSize();
    int halfW = size.width() / 2;
    int halfH = size.height() / 2;

    QRectF rect(m_position.x() - halfW, m_position.y() - halfH,
                size.width(), size.height());

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
    QSize size = emojiSize();
    int margin = 4;
    int halfW = size.width() / 2 + margin;
    int halfH = size.height() / 2 + margin;

    return QRect(m_position.x() - halfW, m_position.y() - halfH,
                 halfW * 2, halfH * 2);
}

std::unique_ptr<AnnotationItem> EmojiStickerAnnotation::clone() const
{
    return std::make_unique<EmojiStickerAnnotation>(m_position, m_emoji, m_scale);
}
