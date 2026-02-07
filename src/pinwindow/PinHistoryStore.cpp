#include "pinwindow/PinHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace {
constexpr auto kEnvPinHistoryPath = "SNAPTRAY_PIN_HISTORY_DIR";
const QRegularExpression kCacheNamePattern(
    QStringLiteral("^cache_(\\d{8}_\\d{6}_\\d{3})\\.png$"));
}

QString PinHistoryStore::historyFolderPath()
{
    const QByteArray overridden = qgetenv(kEnvPinHistoryPath);
    if (!overridden.isEmpty()) {
        return QString::fromLocal8Bit(overridden);
    }

    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(basePath).filePath("pinwindow_history");
}

QList<PinHistoryEntry> PinHistoryStore::loadEntries()
{
    return loadEntriesFromFolder(historyFolderPath());
}

QList<PinHistoryEntry> PinHistoryStore::loadEntriesFromFolder(const QString& folderPath)
{
    QDir dir(folderPath);
    if (!dir.exists()) {
        return {};
    }

    const QFileInfoList files = dir.entryInfoList(
        QStringList() << "cache_*.png",
        QDir::Files | QDir::Readable,
        QDir::Name);

    QList<PinHistoryEntry> entries;
    entries.reserve(files.size());

    for (const QFileInfo& fileInfo : files) {
        PinHistoryEntry entry;
        entry.imagePath = fileInfo.filePath();
        entry.metadataPath = regionMetadataPathForImage(entry.imagePath);
        entry.dprMetadataPath = dprMetadataPathForImage(entry.imagePath);
        entry.capturedAt = parseCapturedAt(fileInfo.fileName(), fileInfo.lastModified());
        entry.devicePixelRatio = parseDevicePixelRatio(entry.dprMetadataPath);

        QImageReader reader(entry.imagePath);
        entry.imageSize = reader.size();

        if (!entry.imageSize.isValid()) {
            const QImage image(entry.imagePath);
            if (!image.isNull()) {
                entry.imageSize = image.size();
            }
        }

        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const PinHistoryEntry& lhs, const PinHistoryEntry& rhs) {
        if (lhs.capturedAt != rhs.capturedAt) {
            return lhs.capturedAt > rhs.capturedAt;
        }
        return lhs.imagePath > rhs.imagePath;
    });

    return entries;
}

QString PinHistoryStore::regionMetadataPathForImage(const QString& imagePath)
{
    const QFileInfo fileInfo(imagePath);
    return fileInfo.dir().filePath(fileInfo.completeBaseName() + ".snpr");
}

QString PinHistoryStore::dprMetadataPathForImage(const QString& imagePath)
{
    const QFileInfo fileInfo(imagePath);
    return fileInfo.dir().filePath(fileInfo.completeBaseName() + ".snpd");
}

bool PinHistoryStore::deleteEntry(const PinHistoryEntry& entry)
{
    bool success = true;

    if (!entry.imagePath.isEmpty() && QFile::exists(entry.imagePath)) {
        success = QFile::remove(entry.imagePath) && success;
    }

    if (!entry.metadataPath.isEmpty() && QFile::exists(entry.metadataPath)) {
        success = QFile::remove(entry.metadataPath) && success;
    }

    if (!entry.dprMetadataPath.isEmpty() && QFile::exists(entry.dprMetadataPath)) {
        success = QFile::remove(entry.dprMetadataPath) && success;
    }

    return success;
}

QDateTime PinHistoryStore::parseCapturedAt(const QString& fileName, const QDateTime& fallbackTime)
{
    const QRegularExpressionMatch match = kCacheNamePattern.match(fileName);
    if (match.hasMatch()) {
        const QString timestamp = match.captured(1);
        const QDateTime parsed = QDateTime::fromString(timestamp, "yyyyMMdd_HHmmss_zzz");
        if (parsed.isValid()) {
            return parsed;
        }
    }

    return fallbackTime;
}

qreal PinHistoryStore::parseDevicePixelRatio(const QString& dprMetadataPath)
{
    QFile file(dprMetadataPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 1.0;
    }

    QTextStream stream(&file);
    const QString value = stream.readLine().trimmed();
    bool ok = false;
    const qreal dpr = value.toDouble(&ok);
    if (!ok || dpr <= 0.0) {
        return 1.0;
    }
    return dpr;
}
