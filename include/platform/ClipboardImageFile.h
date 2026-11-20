#pragma once

#include <QImage>
#include <QList>
#include <QLockFile>
#include <QUrl>
#include <functional>
#include <memory>
#include <optional>

class QMimeData;

namespace SnapTray {
inline constexpr qint64 kClipboardImageQuota = 512LL * 1024 * 1024;

using ClipboardUrls = std::function<std::optional<QList<QUrl>>()>;
enum class ClipboardStoreResult { Success, NeedsSpace, Failed };

// An unpublished snapshot is removed when preparation is cancelled or superseded.
struct ClipboardImageFile {
    QString path;
    QByteArray png;
    bool offeredToClipboard = false;
    // Other processes must leave this file alone until publication finishes.
    std::unique_ptr<QLockFile> publicationLock;
    ClipboardStoreResult save(const QString& directory, qint64 quota, const ClipboardUrls& readUrls = {});
    ~ClipboardImageFile();
};

QString clipboardImageDirectory();
std::optional<QList<QUrl>> referencedClipboardFiles(const QMimeData* data);
void cleanClipboardImageStores(const ClipboardUrls& readUrls);
// Read the selection only for eligible expired files. An unknown selection preserves them.
void pruneClipboardImages(const QString& directory, const ClipboardUrls& readUrls);
bool reclaimClipboardImageSpace(const QString& directory, qint64 bytes, const ClipboardUrls& readUrls);
std::shared_ptr<ClipboardImageFile> encodeClipboardImage(const QImage& image);
std::shared_ptr<ClipboardImageFile> prepareClipboardImageFile(
    const QImage& image, const QString& directory, qint64 quota = kClipboardImageQuota,
    const ClipboardUrls& readUrls = {});
}
