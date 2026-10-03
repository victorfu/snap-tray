#include "recording/WindowTimelineSidecar.h"

#include <QDebug>
#include <QFile>
#include <QSaveFile>

namespace SnapTray::WindowTimelineSidecar {

QString pathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kSuffix);
}

bool write(const QString& videoPath, const WindowTimeline& timeline)
{
    QSaveFile file(pathFor(videoPath));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "WindowTimelineSidecar: cannot open" << file.fileName() << file.errorString();
        return false;
    }
    const QByteArray json = timeline.toJson();
    if (file.write(json) != json.size() || !file.commit()) {
        qWarning() << "WindowTimelineSidecar: cannot write" << file.fileName() << file.errorString();
        return false;
    }
    return true;
}

std::optional<WindowTimeline> read(const QString& videoPath)
{
    QFile file(pathFor(videoPath));
    if (!file.exists()) {
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WindowTimelineSidecar: cannot read" << file.fileName() << file.errorString();
        return std::nullopt;
    }
    std::optional<WindowTimeline> timeline = WindowTimeline::fromJson(file.readAll());
    if (!timeline) {
        qDebug() << "WindowTimelineSidecar: ignoring unreadable sidecar" << file.fileName();
    }
    return timeline;
}

void remove(const QString& videoPath)
{
    const QString path = pathFor(videoPath);
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "WindowTimelineSidecar: cannot remove" << path;
    }
}

} // namespace SnapTray::WindowTimelineSidecar
