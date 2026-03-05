#include "qml/SvgIconItem.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <QDebug>

SvgIconItem::SvgIconItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAntialiasing(true);
}

SvgIconItem::~SvgIconItem() = default;

void SvgIconItem::paint(QPainter* painter)
{
    if (!painter || !m_renderer || !m_renderer->isValid()) {
        return;
    }

    const int logicalWidth = qMax(1, qRound(width()));
    const int logicalHeight = qMax(1, qRound(height()));
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;

    QImage image(QSize(qMax(1, qRound(logicalWidth * dpr)),
                       qMax(1, qRound(logicalHeight * dpr))),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter imagePainter(&image);
    imagePainter.setRenderHint(QPainter::Antialiasing, true);
    imagePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_renderer->render(&imagePainter, QRectF(0.0, 0.0, logicalWidth, logicalHeight));
    imagePainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    imagePainter.fillRect(QRectF(0.0, 0.0, logicalWidth, logicalHeight), m_color);
    imagePainter.end();

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(QPointF(0.0, 0.0), image);
}

void SvgIconItem::setSource(const QUrl& source)
{
    if (m_source == source) {
        return;
    }

    m_source = source;
    emit sourceChanged();

    m_renderer.reset();
    const QString sourcePath = resolveSourcePath(source);
    if (!sourcePath.isEmpty()) {
        auto renderer = std::make_unique<QSvgRenderer>(sourcePath);
        if (renderer->isValid()) {
            m_renderer = std::move(renderer);
        } else {
            qWarning() << "SvgIconItem: Failed to load icon:" << sourcePath;
        }
    }

    update();
}

void SvgIconItem::setColor(const QColor& color)
{
    if (m_color == color) {
        return;
    }

    m_color = color;
    emit colorChanged();
    update();
}

QString SvgIconItem::resolveSourcePath(const QUrl& source)
{
    if (!source.isValid() || source.isEmpty()) {
        return QString();
    }

    if (source.scheme() == QLatin1String("qrc")) {
        const QString path = source.path();
        return path.isEmpty() ? QString() : QStringLiteral(":%1").arg(path);
    }

    if (source.isLocalFile()) {
        return source.toLocalFile();
    }

    return source.toString();
}
