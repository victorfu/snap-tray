#include "platform/ClipboardImageFile.h"

#include <QBuffer>
#include <QDebug>
#include <QImageWriter>
#include <QRegularExpression>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QLockFile>
#include <QMimeData>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#ifdef Q_OS_LINUX
#include <QLocalSocket>
#endif

namespace SnapTray {
namespace {
constexpr int kRetentionDays = 7;
constexpr int kStoreLockTimeoutMs = 1000;
const QStringList kSnapshotFilter{QStringLiteral("Screenshot-*.png")};
bool isSnapshot(const QFileInfo& file)
{
    const QString id = file.completeBaseName().mid(QStringLiteral("Screenshot-").size());
    return !QUuid::fromString(id).isNull();
}

constexpr auto kPrivateDirectory = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
}

ClipboardImageFile::~ClipboardImageFile()
{
    if (!offeredToClipboard && !path.isEmpty())
        QFile::remove(path);
}

QString clipboardImageDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty())
        return {};
    QString directory = QDir(base).filePath(QStringLiteral("clipboard-images"));
#ifdef Q_OS_LINUX
    // Independent X servers can keep different snapshots live for the same user.
    const auto display = qEnvironmentVariable("DISPLAY");
    static const QRegularExpression local(QStringLiteral("^(?:unix)?:([0-9]+)(?:\\.[0-9]+)?$"));
    const auto match = local.match(display);
    const QString id = match.hasMatch() ? QStringLiteral("local-") + match.captured(1)
        : QString::fromLatin1(QCryptographicHash::hash(display.toUtf8(), QCryptographicHash::Sha256).toHex());
    directory = QDir(directory).filePath(id);
#endif
    return directory;
}

std::optional<QList<QUrl>> referencedClipboardFiles(const QMimeData* data)
{
    if (!data)
        return std::nullopt;
    const auto formats = data->formats();
    if (formats.isEmpty())
        return std::nullopt;
    auto urls = data->urls();
    if (formats.contains(QStringLiteral("text/uri-list")) && urls.isEmpty())
        return std::nullopt;
    for (const auto& url : urls) {
        if (!url.isValid() || url.isRelative())
            return std::nullopt;
    }
    const QString copiedFiles = QStringLiteral("x-special/gnome-copied-files");
    if (formats.contains(copiedFiles)) {
        const auto lines = data->data(copiedFiles).trimmed().split('\n');
        if (lines.size() < 2 || (lines.first() != "copy" && lines.first() != "cut"))
            return std::nullopt;
        for (const auto& entry : lines.sliced(1)) {
            const auto url = QUrl::fromEncoded(entry.trimmed(), QUrl::StrictMode);
            if (!url.isValid() || url.isRelative())
                return std::nullopt;
            urls.append(url);
        }
    }
    return urls;
}

void pruneClipboardImages(const QString& directory, const ClipboardUrls& readUrls)
{
    QDir dir(directory);
    if (directory.isEmpty() || !dir.exists())
        return;
    QLockFile lock(dir.filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock())
        return;
    const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-kRetentionDays);
    // QSaveFile can leave its sibling temporary file behind after a crash.
    // A live writer holds store.lock throughout its write and commit.
    static const QRegularExpression temporary(QStringLiteral(
        "^Screenshot-[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\\.png\\.[A-Za-z0-9]{6}$"));
    for (const auto& file : dir.entryInfoList({QStringLiteral("Screenshot-*.png.??????")}, QDir::Files | QDir::NoSymLinks)) {
        if (file.lastModified() < cutoff && temporary.match(file.fileName()).hasMatch())
            QFile::remove(file.absoluteFilePath());
    }
    std::optional<QList<QUrl>> referenced;
    for (const auto& file : dir.entryInfoList(kSnapshotFilter, QDir::Files | QDir::NoSymLinks)) {
        if (!isSnapshot(file) || file.lastModified() >= cutoff)
            continue;
        QLockFile publication(file.absoluteFilePath() + QStringLiteral(".lock"));
        if (!publication.tryLock())
            continue;
        if (!referenced) {
            referenced = readUrls();
            if (!referenced)
                return;
        }
        if (!referenced->contains(QUrl::fromLocalFile(file.absoluteFilePath())))
            QFile::remove(file.absoluteFilePath());
    }
}

void cleanClipboardImageStores(const ClipboardUrls& readUrls)
{
    const QString directory = clipboardImageDirectory();
    if (directory.isEmpty())
        return;
    pruneClipboardImages(directory, readUrls);
#ifdef Q_OS_LINUX
    // A terminated local X server cannot still own a clipboard. Leave
    // reachable or indeterminate displays alone, including remote servers.
    QDir parent = QFileInfo(directory).dir();
    static const QRegularExpression local(QStringLiteral("^local-([0-9]+)$"));
    for (const auto& entry : parent.entryInfoList({QStringLiteral("local-*")}, QDir::Dirs | QDir::NoSymLinks)) {
        const auto match = local.match(entry.fileName());
        if (!match.hasMatch() || entry.absoluteFilePath() == directory)
            continue;
        const QString socketName = QStringLiteral("/tmp/.X11-unix/X") + match.captured(1);
        if (QFileInfo::exists(socketName))
            continue;
        QLocalSocket socket;
        socket.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
        socket.connectToServer(socketName);
        if (!socket.waitForConnected(0)
            && (socket.error() == QLocalSocket::ServerNotFoundError
                || socket.error() == QLocalSocket::ConnectionRefusedError))
            pruneClipboardImages(entry.absoluteFilePath(), readUrls);
    }
#endif
}

std::shared_ptr<ClipboardImageFile> encodeClipboardImage(const QImage& image)
{
    auto snapshot = std::make_shared<ClipboardImageFile>();
    QBuffer buffer(&snapshot->png);
    if (!buffer.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot open clipboard PNG buffer:" << buffer.errorString();
        return {};
    }
    QImageWriter writer(&buffer, "PNG");
    if (!writer.write(image)) {
        qWarning() << "Clipboard PNG encoding failed:" << writer.errorString();
        return {};
    }
    return snapshot;
}

namespace {
ClipboardStoreResult checkClipboardSpace(const QDir& dir, qint64 bytes, qint64 quota, const ClipboardUrls& readUrls)
{
    const auto files = dir.entryInfoList(kSnapshotFilter, QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed);
    qint64 required = bytes;
    for (const auto& file : files)
        required += file.size();
    if (required > quota) {
        if (!readUrls)
            return ClipboardStoreResult::NeedsSpace;
        // Exclude pending publications before sampling the clipboard. A worker
        // may finish publishing while the selection is being read.
        QFileInfoList candidates;
        for (const auto& file : files) {
            if (!isSnapshot(file))
                continue;
            QLockFile publication(file.absoluteFilePath() + QStringLiteral(".lock"));
            if (publication.tryLock())
                candidates.append(file);
        }
        const auto referenced = readUrls();
        if (referenced) {
            for (const auto& file : candidates) {
                if (referenced->contains(QUrl::fromLocalFile(file.absoluteFilePath()))
                    || !QFile::remove(file.absoluteFilePath()))
                    continue;
                required -= file.size();
                if (required <= quota)
                    break;
            }
        }
        if (required > quota) {
            qWarning() << "Clipboard PNG quota exhausted:" << dir.path() << required << "bytes needed, quota" << quota;
            return ClipboardStoreResult::Failed;
        }
    }
    return ClipboardStoreResult::Success;
}
}

bool reclaimClipboardImageSpace(const QString& directory, qint64 bytes, const ClipboardUrls& readUrls)
{
    QDir dir(directory);
    QLockFile lock(dir.filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(kStoreLockTimeoutMs)) {
        qWarning() << "Cannot lock clipboard directory for cleanup:" << directory << lock.error();
        return false;
    }
    return checkClipboardSpace(dir, bytes, kClipboardImageQuota, readUrls) == ClipboardStoreResult::Success;
}

ClipboardStoreResult ClipboardImageFile::save(const QString& directory, qint64 quota,
                                              const ClipboardUrls& readUrls)
{
    if (directory.isEmpty() || png.size() > quota) {
        qWarning() << "Clipboard PNG cannot be stored:" << directory << png.size() << "bytes, quota" << quota;
        return ClipboardStoreResult::Failed;
    }
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral("."), kPrivateDirectory)) {
        qWarning() << "Cannot create clipboard directory:" << directory;
        return ClipboardStoreResult::Failed;
    }
    QLockFile lock(dir.filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(kStoreLockTimeoutMs)) {
        qWarning() << "Cannot lock clipboard directory:" << directory << lock.error();
        return ClipboardStoreResult::Failed;
    }
    const auto space = checkClipboardSpace(dir, png.size(), quota, readUrls);
    if (space != ClipboardStoreResult::Success)
        return space;
    const QString destination = dir.filePath(QStringLiteral("Screenshot-%1.png")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    auto publication = std::make_unique<QLockFile>(destination + QStringLiteral(".lock"));
    if (!publication->tryLock()) {
        qWarning() << "Cannot lock clipboard PNG:" << destination << publication->error();
        return ClipboardStoreResult::Failed;
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(png) != png.size() || !file.commit()) {
        qWarning() << "Cannot write clipboard PNG:" << destination << png.size() << "bytes:" << file.errorString();
        return ClipboardStoreResult::Failed;
    }
    path = destination;
    publicationLock = std::move(publication);
    return ClipboardStoreResult::Success;
}

std::shared_ptr<ClipboardImageFile> prepareClipboardImageFile(const QImage& image, const QString& directory,
                                                             qint64 quota, const ClipboardUrls& readUrls)
{
    auto file = encodeClipboardImage(image);
    if (!file)
        return {};
    const auto result = file->save(directory, quota, readUrls);
    return result == ClipboardStoreResult::Success ? file : nullptr;
}
}
