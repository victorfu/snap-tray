#pragma once

#include <QImage>
#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QVector>

namespace SnapTray {

struct ScreenSource {
    QString id;
    QString name;
    QString resolutionText;
    QRect geometry;
    qreal devicePixelRatio = 1.0;
    bool isPrimary = false;
    QImage thumbnail;
    QPointer<QScreen> screen;
};

using ScreenSourceInfo = ScreenSource;

class ScreenSourceService final
{
public:
    static QVector<ScreenSource> availableSources(bool includeThumbnails = false);

    static QString screenId(const QScreen* screen);
    static QString sourceId(const QScreen* screen, int displayIndex);
    static QString displayName(const QScreen* screen, int displayIndex);
    static QString resolutionText(const QRect& geometry, qreal devicePixelRatio = 1.0);
    static QScreen* screenForId(const QString& id);

private:
    static QImage captureThumbnail(QScreen* screen);
};

} // namespace SnapTray
