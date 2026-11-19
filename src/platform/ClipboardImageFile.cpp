#include "platform/ClipboardImageFile.h"

#include <QBuffer>
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
#include <QRegularExpression>
#endif

namespace SnapTray {
namespace {
constexpr int kRetentionDays = 7;
constexpr int kStoreLockTimeoutMs = 1000;
const QStringList kSnapshotFilter{QStringLiteral("Screenshot-*.png")};
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
    std::optional<QList<QUrl>> referenced;
    for (const auto& file : dir.entryInfoList(kSnapshotFilter, QDir::Files | QDir::NoSymLinks)) {
        const QString id = file.completeBaseName().mid(QStringLiteral("Screenshot-").size());
        if (QUuid::fromString(id).isNull() || file.lastModified() >= cutoff)
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

std::shared_ptr<ClipboardImageFile> prepareClipboardImageFile(const QImage& image,
                                                             const QString& directory,
                                                             qint64 quota)
{
    if (directory.isEmpty() || image.isNull())
        return {};
    auto snapshot = std::make_shared<ClipboardImageFile>();
    QBuffer buffer(&snapshot->png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || snapshot->png.size() > quota)
        return {};

    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral("."), kPrivateDirectory))
        return {};
    QLockFile lock(dir.filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(kStoreLockTimeoutMs))
        return {};
    qint64 required = snapshot->png.size();
    for (const auto& file : dir.entryInfoList(kSnapshotFilter, QDir::Files | QDir::NoSymLinks)) {
        required += file.size();
        if (required > quota)
            return {};
    }
    const QString path = dir.filePath(QStringLiteral("Screenshot-%1.png")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(snapshot->png) != snapshot->png.size() || !file.commit())
        return {};
    snapshot->path = path;
    return snapshot;
}
}
