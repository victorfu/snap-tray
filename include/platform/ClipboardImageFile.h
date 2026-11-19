#pragma once

#include <QImage>
#include <QList>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

class QMimeData;

namespace SnapTray {
inline constexpr qint64 kClipboardImageQuota = 512LL * 1024 * 1024;

// An unpublished snapshot is removed when preparation is cancelled or superseded.
struct ClipboardImageFile {
    QString path;
    QByteArray png;
    bool offeredToClipboard = false;
    ~ClipboardImageFile();
};

QString clipboardImageDirectory();
// Read the current selection only if there is an expired candidate. An
// unavailable clipboard leaves every candidate intact.
using ClipboardUrls = std::function<std::optional<QList<QUrl>>()>;
std::optional<QList<QUrl>> referencedClipboardFiles(const QMimeData* data);
void cleanClipboardImageStores(const ClipboardUrls& readUrls);
void pruneClipboardImages(const QString& directory, const ClipboardUrls& readUrls);
std::shared_ptr<ClipboardImageFile> prepareClipboardImageFile(
    const QImage& image, const QString& directory, qint64 quota = kClipboardImageQuota);
}
