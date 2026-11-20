#pragma once

#include <QImage>
#include <QStringList>
#include <optional>

namespace SnapTray {
inline constexpr char kClipboardOwnerArgument[] = "--internal-clipboard-owner";
inline constexpr int kClipboardHandoffTimeoutMs = 10000;
bool isLinuxClipboardOwnerRequest(const QStringList& arguments);
// Requires a QGuiApplication and an X11 session; does not initialize the GUI.
int runLinuxClipboardOwner(const QStringList& arguments);
std::optional<quint32> linuxClipboardOwnerWindow();
bool copyFileToLinuxClipboard(const QString& path, bool includePixels, const QString& launchPath,
                              bool& publicationStarted, quint32 expectedOwner = 0);
bool copyImageToLinuxClipboard(const QImage& image, const QString& launchPath,
                              int timeoutMs = kClipboardHandoffTimeoutMs);
}
