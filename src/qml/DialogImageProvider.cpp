#include "qml/DialogImageProvider.h"

namespace SnapTray {

QMutex DialogImageProvider::s_mutex;
QHash<QString, QImage> DialogImageProvider::s_images;

DialogImageProvider::DialogImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DialogImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QMutexLocker lock(&s_mutex);
    // Strip cache-buster suffix (e.g. "region_1?3" → "region_1")
    const QString cleanId = id.section(QLatin1Char('?'), 0, 0);
    const QImage img = s_images.value(cleanId);
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
    QMutexLocker lock(&s_mutex);
    s_images.insert(id, image);
}

void DialogImageProvider::removeImage(const QString& id)
{
    QMutexLocker lock(&s_mutex);
    s_images.remove(id);
}

} // namespace SnapTray
