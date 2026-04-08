#include "recording/ScreenSourceService.h"

#include "capture/ScreenSnapshot.h"
#include "settings/ScreenCanvasSettingsManager.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QStringList>

namespace {

constexpr QSize kThumbnailSize(320, 200);

} // namespace

namespace SnapTray {

QVector<ScreenSource> ScreenSourceService::availableSources(bool includeThumbnails)
{
    QVector<ScreenSourceInfo> sources;

    const QList<QScreen*> screens = QGuiApplication::screens();
    sources.reserve(screens.size());

    for (int index = 0; index < screens.size(); ++index) {
        QScreen* screen = screens.at(index);
        if (!screen) {
            continue;
        }

        ScreenSourceInfo info;
        info.id = sourceId(screen, index);
        info.name = displayName(screen, index);
        info.geometry = screen->geometry();
        info.devicePixelRatio = screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
        info.resolutionText = resolutionText(screen->geometry(), info.devicePixelRatio);
        info.isPrimary = (screen == QGuiApplication::primaryScreen());
        info.screen = screen;
        if (includeThumbnails) {
            info.thumbnail = captureThumbnail(screen);
        }
        sources.append(info);
    }

    return sources;
}

QString ScreenSourceService::screenId(const QScreen* screen)
{
    return ScreenCanvasSettingsManager::screenIdentifier(screen);
}

QString ScreenSourceService::sourceId(const QScreen* screen, int displayIndex)
{
    const QString stableId = screenId(screen);
    if (!stableId.isEmpty()) {
        return stableId;
    }

    const QRect geometry = screen ? screen->geometry() : QRect();
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(displayName(screen, displayIndex))
        .arg(geometry.x())
        .arg(geometry.y())
        .arg(geometry.width())
        .arg(geometry.height());
}

QString ScreenSourceService::displayName(const QScreen* screen, int index)
{
    if (!screen) {
        return QStringLiteral("Display %1").arg(index + 1);
    }

    const QString manufacturer = screen->manufacturer().trimmed();
    const QString model = screen->model().trimmed();
    const QString name = screen->name().trimmed();

    QStringList parts;
    if (!manufacturer.isEmpty()) {
        parts << manufacturer;
    }
    if (!model.isEmpty() && model.compare(manufacturer, Qt::CaseInsensitive) != 0) {
        parts << model;
    }

    QString label = parts.join(QLatin1Char(' ')).trimmed();
    if (label.isEmpty()) {
        label = !name.isEmpty()
            ? name
            : QStringLiteral("Display %1").arg(index + 1);
    } else if (!name.isEmpty() && !label.contains(name, Qt::CaseInsensitive)) {
        label = QStringLiteral("%1 (%2)").arg(label, name);
    }

    return label;
}

QString ScreenSourceService::resolutionText(const QRect& geometry, qreal devicePixelRatio)
{
    if (!geometry.isValid() || geometry.isEmpty()) {
        return {};
    }

    const qreal safeDpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const int width = qRound(static_cast<qreal>(geometry.width()) * safeDpr);
    const int height = qRound(static_cast<qreal>(geometry.height()) * safeDpr);
    return QStringLiteral("%1 x %2").arg(width).arg(height);
}

QScreen* ScreenSourceService::screenForId(const QString& id)
{
    if (id.trimmed().isEmpty()) {
        return nullptr;
    }

    for (QScreen* screen : QGuiApplication::screens()) {
        if (screenId(screen) == id) {
            return screen;
        }
    }

    return nullptr;
}

QImage ScreenSourceService::captureThumbnail(QScreen* screen)
{
    if (!screen) {
        return {};
    }

    const QPixmap snapshot = snaptray::capture::captureScreenSnapshot(screen);
    if (snapshot.isNull()) {
        return {};
    }

    return snapshot.toImage().scaled(kThumbnailSize,
                                     Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
}

} // namespace SnapTray
