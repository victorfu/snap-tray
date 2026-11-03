#include "qml/QuickSaveAction.h"

#include "settings/RecordingSettingsManager.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>

namespace SnapTray::QuickSaveAction {

QMimeData* fileMimeData(const QString& path)
{
    auto* data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath())});
    data->setText(QFileInfo(path).absoluteFilePath());
    return data;
}

bool apply(int action, const QString& path, const std::function<bool(const QUrl&)>& openUrl)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    switch (action) {
    case RecordingSettingsManager::QuickSaveCopyFile:
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setMimeData(fileMimeData(path));
            return true;
        }
        return false;
    case RecordingSettingsManager::QuickSaveShowInFolder: {
        const QUrl folder = QUrl::fromLocalFile(QFileInfo(path).absolutePath());
        return openUrl ? openUrl(folder) : QDesktopServices::openUrl(folder);
    }
    default:
        return false;
    }
}

} // namespace SnapTray::QuickSaveAction
