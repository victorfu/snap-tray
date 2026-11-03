#pragma once

#include <QString>
#include <QUrl>

#include <functional>

class QMimeData;

// What quick mode (#193) does once a recording is saved: nothing, put the
// file on the clipboard (to paste into a chat or folder), or show its folder.
namespace SnapTray::QuickSaveAction {

// The file as the clipboard carries it: its URL, and its path as text.
QMimeData* fileMimeData(const QString& path);

// Runs `action` (RecordingSettingsManager::QuickSaveAction) for a saved
// file; `openUrl` is QDesktopServices::openUrl unless a test passes one.
// False when there was nothing to do or it failed.
bool apply(int action, const QString& path, const std::function<bool(const QUrl&)>& openUrl = {});

} // namespace SnapTray::QuickSaveAction
