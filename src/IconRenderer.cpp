#include "IconRenderer.h"
#include "utils/CoordinateHelper.h"

#include <QPainter>
#include <QSvgRenderer>
#include <QPixmap>
#include <QDebug>

size_t qHash(const IconRenderer::PixmapCacheKey& key, size_t seed)
{
    return qHash(key.iconKey, seed) ^ qHash(key.width) ^ qHash(key.height)
         ^ qHash(key.color) ^ qHash(key.dpr100);
}

IconRenderer& IconRenderer::instance()
{
    static IconRenderer instance;
    return instance;
}

IconRenderer::IconRenderer()
{
}

IconRenderer::~IconRenderer()
{
    clearCache();
}

bool IconRenderer::loadIcon(const QString& key, const QString& resourcePath)
{
    // Check if already loaded
    if (m_renderers.contains(key)) {
        return true;
    }

    QSvgRenderer* renderer = new QSvgRenderer(resourcePath);
    if (renderer->isValid()) {
        m_renderers[key] = renderer;
        return true;
    } else {
        qWarning() << "IconRenderer: Failed to load icon:" << resourcePath;
        delete renderer;
        return false;
    }
}

bool IconRenderer::loadIconByKey(const QString& key)
{
    return loadIcon(key, QString(":/icons/icons/%1.svg").arg(key));
}

void IconRenderer::loadIconsByKey(const QStringList& keys)
{
    for (const QString& key : keys) {
        if (!key.isEmpty()) {
            loadIconByKey(key);
        }
    }
}

bool IconRenderer::hasIcon(const QString& key) const
{
    return m_renderers.contains(key);
}

void IconRenderer::renderIcon(QPainter& painter, const QRect& rect,
                               const QString& key, const QColor& tintColor,
                               int padding)
{
    QSvgRenderer* renderer = m_renderers.value(key, nullptr);
    if (!renderer) {
        // Fallback: draw a question mark
        painter.setPen(tintColor);
        painter.drawText(rect, Qt::AlignCenter, "?");
        return;
    }

    // Calculate icon size with padding
    int iconSize = qMin(rect.width(), rect.height()) - padding;
    QRect iconRect(0, 0, iconSize, iconSize);
    iconRect.moveCenter(rect.center());

    // Get device pixel ratio for HiDPI support
    qreal dpr = painter.device()->devicePixelRatio();

    // Build cache key
    PixmapCacheKey cacheKey{key, iconSize, iconSize, tintColor.rgba(),
                            static_cast<int>(dpr * 100)};

    // Check cache
    auto it = m_pixmapCache.find(cacheKey);
    if (it != m_pixmapCache.end()) {
        painter.drawPixmap(iconRect, it.value());
        return;
    }

    // Cache miss: render SVG to pixmap
    QSize physSize = CoordinateHelper::toPhysical(QSize(iconSize, iconSize), dpr);
    QPixmap iconPixmap(physSize);
    iconPixmap.setDevicePixelRatio(dpr);
    iconPixmap.fill(Qt::transparent);

    QPainter iconPainter(&iconPixmap);
    iconPainter.setRenderHint(QPainter::Antialiasing);
    renderer->render(&iconPainter, QRectF(0, 0, iconSize, iconSize));
    iconPainter.end();

    // Create tinted version using composition
    QPixmap tintedPixmap(iconPixmap.size());
    tintedPixmap.setDevicePixelRatio(dpr);
    tintedPixmap.fill(Qt::transparent);

    QPainter tintPainter(&tintedPixmap);
    tintPainter.drawPixmap(0, 0, iconPixmap);
    tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tintPainter.fillRect(tintedPixmap.rect(), tintColor);
    tintPainter.end();

    // Store in cache and draw
    m_pixmapCache.insert(cacheKey, tintedPixmap);
    painter.drawPixmap(iconRect, tintedPixmap);
}

void IconRenderer::renderIconFit(QPainter& painter, const QRect& rect,
                                  const QString& key, const QColor& tintColor)
{
    QSvgRenderer* renderer = m_renderers.value(key, nullptr);
    if (!renderer) {
        painter.setPen(tintColor);
        painter.drawText(rect, Qt::AlignCenter, "?");
        return;
    }

    // Get SVG default size and scale to fit the rect while preserving aspect ratio
    QSizeF svgSize = renderer->defaultSize();
    QSizeF targetSize = svgSize.scaled(rect.size(), Qt::KeepAspectRatio);
    QRectF iconRect(0, 0, targetSize.width(), targetSize.height());
    iconRect.moveCenter(QPointF(rect.center()));

    int iconWidth = static_cast<int>(targetSize.width());
    int iconHeight = static_cast<int>(targetSize.height());

    // Get device pixel ratio for HiDPI support
    qreal dpr = painter.device()->devicePixelRatio();

    // Build cache key
    PixmapCacheKey cacheKey{key, iconWidth, iconHeight, tintColor.rgba(),
                            static_cast<int>(dpr * 100)};

    // Check cache
    auto it = m_pixmapCache.find(cacheKey);
    if (it != m_pixmapCache.end()) {
        painter.drawPixmap(iconRect.toRect(), it.value());
        return;
    }

    // Cache miss: render SVG to pixmap
    QSize physSize = CoordinateHelper::toPhysical(QSize(iconWidth, iconHeight), dpr);
    QPixmap iconPixmap(physSize);
    iconPixmap.setDevicePixelRatio(dpr);
    iconPixmap.fill(Qt::transparent);

    QPainter iconPainter(&iconPixmap);
    iconPainter.setRenderHint(QPainter::Antialiasing);
    renderer->render(&iconPainter, QRectF(0, 0, iconWidth, iconHeight));
    iconPainter.end();

    // Create tinted version using composition
    QPixmap tintedPixmap(iconPixmap.size());
    tintedPixmap.setDevicePixelRatio(dpr);
    tintedPixmap.fill(Qt::transparent);

    QPainter tintPainter(&tintedPixmap);
    tintPainter.drawPixmap(0, 0, iconPixmap);
    tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tintPainter.fillRect(tintedPixmap.rect(), tintColor);
    tintPainter.end();

    // Store in cache and draw
    m_pixmapCache.insert(cacheKey, tintedPixmap);
    painter.drawPixmap(iconRect.toRect(), tintedPixmap);
}

void IconRenderer::clearCache()
{
    qDeleteAll(m_renderers);
    m_renderers.clear();
    m_pixmapCache.clear();
}
