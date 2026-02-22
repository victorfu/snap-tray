#include "annotations/EmojiStickerAnnotation.h"
#include <QFontMetrics>
#include <QPainter>
#include <cmath>
#include <QtMath>

namespace {
QFont emojiFontForScale(qreal scale)
{
    const int fontSize = qMax(1, qRound(EmojiStickerAnnotation::kBaseSize * scale));

    QFont font;
#ifdef Q_OS_MAC
    font.setFamily("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    font.setFamily("Segoe UI Emoji");
#else
    font.setFamily("Noto Color Emoji");
#endif
    font.setPointSize(fontSize);
    return font;
}

struct EmojiGlyphLayout
{
    QFont font;
    QRect textRect;
    QPointF drawOrigin; // Baseline origin passed to QPainter::drawText.
    QRectF inkRect;     // Glyph bounding rect in widget coordinates.
};

EmojiGlyphLayout computeEmojiGlyphLayout(const QPoint& center, const QString& emoji, qreal scale)
{
    EmojiGlyphLayout layout;
    layout.font = emojiFontForScale(scale);

    QFontMetrics fm(layout.font);
    layout.textRect = fm.boundingRect(emoji);

    const qreal originX = static_cast<qreal>(center.x()) -
                          (layout.textRect.x() + layout.textRect.width() / 2.0);
    const qreal originY = static_cast<qreal>(center.y()) -
                          (layout.textRect.y() + layout.textRect.height() / 2.0);

    layout.drawOrigin = QPointF(originX, originY);
    layout.inkRect = QRectF(layout.drawOrigin.x() + layout.textRect.x(),
                            layout.drawOrigin.y() + layout.textRect.y(),
                            layout.textRect.width(),
                            layout.textRect.height());
    return layout;
}
} // namespace

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
    const EmojiGlyphLayout layout = computeEmojiGlyphLayout(m_position, m_emoji, m_scale);
    return layout.inkRect;
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

    const EmojiGlyphLayout layout = computeEmojiGlyphLayout(m_position, m_emoji, m_scale);
    painter.setFont(layout.font);

    QRectF rect = layout.inkRect;
    QPointF c = rect.center();
    painter.translate(c);
    painter.setTransform(localLinearTransform(), true);
    painter.translate(-c);

    painter.drawText(layout.drawOrigin, m_emoji);

    painter.restore();
}

QRect EmojiStickerAnnotation::boundingRect() const
{
    QPolygonF poly = transformedBoundingPolygon();
    QRect rect = poly.boundingRect().toAlignedRect();
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

void EmojiStickerAnnotation::translate(const QPointF& delta)
{
    moveBy(delta.toPoint());
}
