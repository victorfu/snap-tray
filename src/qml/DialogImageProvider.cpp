#include "qml/DialogImageProvider.h"

namespace SnapTray {

QMutex& DialogImageProvider::mutex()
{
    static auto* s_mutex = new QMutex();
    return *s_mutex;
}

QHash<QString, QImage>& DialogImageProvider::images()
{
    static auto* s_images = new QHash<QString, QImage>();
    return *s_images;
}

DialogImageProvider::DialogImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DialogImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QMutexLocker lock(&mutex());
    // Strip cache-buster suffix (e.g. "region_1?3" → "region_1")
    const QString cleanId = id.section(QLatin1Char('?'), 0, 0);
    const QImage img = images().value(cleanId);
    if (img.isNull()) {
        if (size)
            *size = QSize();
        return {};
    }

    if (size)
        *size = img.size();

    if (requestedSize.isValid() && requestedSize != img.size())
        return img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return img;
}

void DialogImageProvider::setImage(const QString& id, const QImage& image)
{
    QMutexLocker lock(&mutex());
    images().insert(id, image);
}

void DialogImageProvider::removeImage(const QString& id)
{
    QMutexLocker lock(&mutex());
    images().remove(id);
}

} // namespace SnapTray
