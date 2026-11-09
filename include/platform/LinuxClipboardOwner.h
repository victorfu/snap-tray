#pragma once

#include <QImage>
#include <QStringList>

namespace SnapTray {
inline constexpr char kClipboardOwnerArgument[] = "--internal-clipboard-owner";
inline constexpr int kClipboardHandoffTimeoutMs = 10000;
bool isLinuxClipboardOwnerRequest(const QStringList& arguments);
// Requires a QGuiApplication and an X11 session; does not initialize the GUI.
int runLinuxClipboardOwner(const QStringList& arguments);
bool copyImageToLinuxClipboard(const QImage& image, const QString& launchPath,
                              int timeoutMs = kClipboardHandoffTimeoutMs);
}
