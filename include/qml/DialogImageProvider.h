#pragma once

#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>
#include <QHash>
#include <QString>

namespace SnapTray {

class DialogImageProvider : public QQuickImageProvider
{
public:
    DialogImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    static void setImage(const QString& id, const QImage& image);
    static void removeImage(const QString& id);

private:
    static QMutex& mutex();
    static QHash<QString, QImage>& images();
};

} // namespace SnapTray
