#include "annotations/EmojiStickerAnnotation.h"
#include "utils/CoordinateHelper.h"
#include <QFontMetrics>
#include <QPainter>
#include <cmath>
#include <QtMath>

namespace {
QFont emojiBaseFont()
{
    QFont font;
#ifdef Q_OS_MAC
    font.setFamily("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    font.setFamily("Segoe UI Emoji");
#else
    font.setFamily("Noto Color Emoji");
#endif
    font.setPointSize(EmojiStickerAnnotation::kBaseSize);
    return font;
}

struct EmojiGlyphLayout
{
    QFont font;
    QRect textRect;
    QPointF drawOrigin; // Baseline origin passed to QPainter::drawText.
    QRectF inkRect;     // Glyph bounding rect in widget coordinates.
};

EmojiGlyphLayout computeEmojiGlyphLayout(const QPointF& center, const QString& emoji)
{
    EmojiGlyphLayout layout;
    layout.font = emojiBaseFont();

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
    if (m_cachedBaseGlyphRect.isNull()) {
        regenerateCache(1.0);
    }

    QRectF rect = m_cachedBaseGlyphRect;
    const QPointF c = QPointF(m_position);
    rect.translate(c - rect.center());
    return rect;
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
    if (!qFuzzyCompare(m_scale, 1.0)) {
        t.scale(m_scale, m_scale);
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

    const qreal dpr = painter.device()->devicePixelRatio();
    if (!isCacheValid(dpr)) {
        regenerateCache(dpr);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF c = QPointF(m_position);
    painter.translate(c);
    painter.setTransform(localLinearTransform(), true);
    painter.translate(-c);
    const QPointF currentOrigin = QPointF(m_position) + m_cachedBaseGlyphRect.topLeft();
    painter.drawPixmap(currentOrigin, m_cachedPixmap);

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

bool EmojiStickerAnnotation::isCacheValid(qreal dpr) const
{
    return !m_cachedPixmap.isNull() &&
           qFuzzyCompare(m_cachedDpr, dpr) &&
           m_cachedEmoji == m_emoji;
}

void EmojiStickerAnnotation::regenerateCache(qreal dpr) const
{
    const EmojiGlyphLayout layout = computeEmojiGlyphLayout(QPointF(0.0, 0.0), m_emoji);
    QRectF inkRect = layout.inkRect;
    if (!inkRect.isValid() || inkRect.isEmpty()) {
        m_cachedPixmap = QPixmap();
        m_cachedOrigin = QPointF();
        m_cachedBaseGlyphRect = QRectF();
        m_cachedDpr = dpr;
        m_cachedEmoji = m_emoji;
        return;
    }

    const QRect alignedInkRect = inkRect.toAlignedRect();
    m_cachedPixmap = QPixmap(CoordinateHelper::toPhysical(alignedInkRect.size(), dpr));
    m_cachedPixmap.setDevicePixelRatio(dpr);
    m_cachedPixmap.fill(Qt::transparent);

    {
        QPainter offPainter(&m_cachedPixmap);
        offPainter.setRenderHint(QPainter::Antialiasing, true);
        offPainter.setRenderHint(QPainter::TextAntialiasing, true);
        offPainter.setFont(layout.font);
        offPainter.drawText(layout.drawOrigin - alignedInkRect.topLeft(), m_emoji);
    }

    m_cachedOrigin = QPointF(m_position) + alignedInkRect.topLeft();
    m_cachedBaseGlyphRect = QRectF(alignedInkRect);
    m_cachedDpr = dpr;
    m_cachedEmoji = m_emoji;
}
