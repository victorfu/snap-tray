#include "qml/SvgIconItem.h"

#include <QFile>
#include <QPainter>
#include <QQuickWindow>
#include <QSvgRenderer>
#include <QDebug>

namespace {

QString fallbackIconResourcePath(const QString& path)
{
    constexpr auto kIconsRoot = ":/icons/";
    constexpr auto kNestedIconsRoot = ":/icons/icons/";
    constexpr qsizetype kIconsRootLength = sizeof(":/icons/") - 1;
    constexpr qsizetype kNestedIconsRootLength = sizeof(":/icons/icons/") - 1;

    if (path.startsWith(QLatin1String(kNestedIconsRoot))) {
        const QString tail = path.mid(kNestedIconsRootLength);
        return QStringLiteral("%1%2").arg(QLatin1String(kIconsRoot), tail);
    }

    if (path.startsWith(QLatin1String(kIconsRoot))) {
        const QString tail = path.mid(kIconsRootLength);
        if (!tail.startsWith(QStringLiteral("icons/"))) {
            return QStringLiteral("%1%2").arg(QLatin1String(kNestedIconsRoot), tail);
        }
    }

    return QString();
}

QString resolveExistingPath(const QString& path)
{
    if (path.isEmpty()) {
        return QString();
    }

    if (!path.startsWith(QStringLiteral(":/"))) {
        return path;
    }

    if (QFile::exists(path)) {
        return path;
    }

    const QString fallbackPath = fallbackIconResourcePath(path);
    if (!fallbackPath.isEmpty() && QFile::exists(fallbackPath)) {
        return fallbackPath;
    }

    return path;
}

} // namespace

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
    const QString sourcePath = resolveExistingPath(resolveSourcePath(source));
    if (!sourcePath.isEmpty()) {
        if (sourcePath.startsWith(QStringLiteral(":/")) && !QFile::exists(sourcePath)) {
            qWarning() << "SvgIconItem: Failed to load icon:" << sourcePath;
            update();
            return;
        }
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
