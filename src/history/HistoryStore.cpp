#include "history/HistoryStore.h"

#include "utils/ImageSaveUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

constexpr auto kEnvHistoryPath = "SNAPTRAY_HISTORY_DIR";
constexpr auto kEntriesFolderName = "entries";
constexpr auto kManifestFileName = "manifest.json";
constexpr auto kCanvasFileName = "canvas.png";
constexpr auto kResultFileName = "result.png";
constexpr auto kAnnotationsFileName = "annotations.json";
constexpr auto kCaptureSessionKind = "capture_session";

QJsonArray serializeRect(const QRect& rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QRect deserializeRect(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QRect(array.size() > 0 ? array.at(0).toInt() : 0,
                 array.size() > 1 ? array.at(1).toInt() : 0,
                 array.size() > 2 ? array.at(2).toInt() : 0,
                 array.size() > 3 ? array.at(3).toInt() : 0);
}

QJsonArray serializeSize(const QSize& size)
{
    return QJsonArray{size.width(), size.height()};
}

QSize deserializeSize(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QSize(array.size() > 0 ? array.at(0).toInt() : 0,
                 array.size() > 1 ? array.at(1).toInt() : 0);
}

QJsonArray serializeColor(const QColor& color)
{
    return QJsonArray{color.red(), color.green(), color.blue(), color.alpha()};
}

QColor deserializeColor(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    return QColor(array.size() > 0 ? array.at(0).toInt() : 0,
                  array.size() > 1 ? array.at(1).toInt() : 0,
                  array.size() > 2 ? array.at(2).toInt() : 0,
                  array.size() > 3 ? array.at(3).toInt() : 255);
}

QJsonArray serializeCaptureRegions(const QVector<MultiRegionManager::Region>& regions)
{
    QJsonArray array;
    for (const auto& region : regions) {
        array.append(QJsonObject{
            {QStringLiteral("rect"), serializeRect(region.rect)},
            {QStringLiteral("color"), serializeColor(region.color)},
            {QStringLiteral("index"), region.index},
            {QStringLiteral("isActive"), region.isActive},
        });
    }
    return array;
}

QVector<MultiRegionManager::Region> deserializeCaptureRegions(const QJsonValue& value)
{
    QVector<MultiRegionManager::Region> regions;
    const QJsonArray array = value.toArray();
    regions.reserve(array.size());
    for (const QJsonValue& regionValue : array) {
        const QJsonObject object = regionValue.toObject();
        MultiRegionManager::Region region;
        region.rect = deserializeRect(object.value(QStringLiteral("rect")));
        region.color = deserializeColor(object.value(QStringLiteral("color")));
        region.index = object.value(QStringLiteral("index")).toInt();
        region.isActive = object.value(QStringLiteral("isActive")).toBool();
        regions.append(region);
    }
    return regions;
}

QString uniqueEntryId(const QDateTime& createdAt)
{
    return QStringLiteral("capture_%1_%2")
        .arg(createdAt.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString fileNameString(const char* value)
{
    return QString::fromLatin1(value);
}

bool writeTextFile(const QString& filePath, const QByteArray& data)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (file.write(data) != data.size()) {
        return false;
    }
    return true;
}

bool saveImage(const QImage& image, const QString& filePath)
{
    if (image.isNull()) {
        return false;
    }

    ImageSaveUtils::Error error;
    return ImageSaveUtils::saveImageAtomically(image, filePath, QByteArrayLiteral("PNG"), &error);
}

QJsonObject baseManifest(const QString& id,
                         const QDateTime& createdAt,
                         const QString& previewPath,
                         const QString& resultPath,
                         bool replayAvailable)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), QString::fromLatin1(kCaptureSessionKind)},
        {QStringLiteral("createdAt"), createdAt.toString(Qt::ISODateWithMs)},
        {QStringLiteral("previewPath"), previewPath},
        {QStringLiteral("resultPath"), resultPath},
        {QStringLiteral("replayAvailable"), replayAvailable},
    };
}

void trimEntriesToLimit(int maxEntries)
{
    if (maxEntries <= 0) {
        return;
    }

    QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    while (entries.size() > maxEntries) {
        const SnapTray::HistoryEntry oldest = entries.takeLast();
        SnapTray::HistoryStore::deleteEntry(oldest);
    }
}

} // namespace

namespace SnapTray {

QString HistoryStore::historyRootPath()
{
    const QByteArray overridden = qgetenv(kEnvHistoryPath);
    if (!overridden.isEmpty()) {
        return QString::fromLocal8Bit(overridden);
    }

    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(basePath).filePath(QStringLiteral("history"));
}

QString HistoryStore::entriesRootPath()
{
    return QDir(historyRootPath()).filePath(fileNameString(kEntriesFolderName));
}

QList<HistoryEntry> HistoryStore::loadEntries()
{
    QList<HistoryEntry> entries;
    QDir entriesDir(entriesRootPath());
    if (!entriesDir.exists()) {
        return entries;
    }

    const QFileInfoList dirs = entriesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& dirInfo : dirs) {
        const auto entry = loadEntryFromDirectory(dirInfo.filePath());
        if (entry.has_value()) {
            entries.append(entry.value());
        }
    }

    std::sort(entries.begin(), entries.end(), [](const HistoryEntry& lhs, const HistoryEntry& rhs) {
        if (lhs.createdAt != rhs.createdAt) {
            return lhs.createdAt > rhs.createdAt;
        }
        return lhs.id > rhs.id;
    });
    return entries;
}

std::optional<HistoryEntry> HistoryStore::loadEntry(const QString& id)
{
    const QString entryDirectory = QDir(entriesRootPath()).filePath(id);
    return loadEntryFromDirectory(entryDirectory);
}

std::optional<HistoryEntry> HistoryStore::writeCaptureSession(const CaptureSessionWriteRequest& request)
{
    if (request.canvasImage.isNull() || request.resultImage.isNull() || !request.selectionRect.isValid()) {
        return std::nullopt;
    }

    QDir root(entriesRootPath());
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        return std::nullopt;
    }

    const QString id = uniqueEntryId(request.createdAt);
    const QString entryDirPath = root.filePath(id);
    if (!root.mkpath(id)) {
        return std::nullopt;
    }

    QDir entryDir(entryDirPath);
    const QString canvasPath = entryDir.filePath(fileNameString(kCanvasFileName));
    const QString resultPath = entryDir.filePath(fileNameString(kResultFileName));
    const QString annotationsPath = entryDir.filePath(fileNameString(kAnnotationsFileName));

    if (!saveImage(request.canvasImage, canvasPath) ||
        !saveImage(request.resultImage, resultPath) ||
        !writeTextFile(annotationsPath, request.annotationsJson)) {
        QDir(entryDirPath).removeRecursively();
        return std::nullopt;
    }

    QJsonObject manifest = baseManifest(id,
                                        request.createdAt,
                                        fileNameString(kResultFileName),
                                        fileNameString(kResultFileName),
                                        true);
    manifest.insert(QStringLiteral("canvasPath"), fileNameString(kCanvasFileName));
    manifest.insert(QStringLiteral("annotationsPath"), fileNameString(kAnnotationsFileName));
    manifest.insert(QStringLiteral("selectionRect"), serializeRect(request.selectionRect));
    manifest.insert(QStringLiteral("captureRegions"), serializeCaptureRegions(request.captureRegions));
    manifest.insert(QStringLiteral("devicePixelRatio"), request.devicePixelRatio);
    manifest.insert(QStringLiteral("canvasLogicalSize"), serializeSize(request.canvasLogicalSize));
    manifest.insert(QStringLiteral("resultSize"), serializeSize(request.resultImage.size()));
    manifest.insert(QStringLiteral("cornerRadius"), request.cornerRadius);

    const QString manifestPath = entryDir.filePath(fileNameString(kManifestFileName));
    if (!writeTextFile(manifestPath, QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
        QDir(entryDirPath).removeRecursively();
        return std::nullopt;
    }

    trimEntriesToLimit(request.maxEntries);
    return loadEntry(id);
}

bool HistoryStore::deleteEntry(const HistoryEntry& entry)
{
    if (entry.entryDirectory.isEmpty()) {
        return false;
    }
    return QDir(entry.entryDirectory).removeRecursively();
}

std::optional<HistoryEntry> HistoryStore::loadEntryFromDirectory(const QString& entryDirectory)
{
    QDir dir(entryDirectory);
    if (!dir.exists()) {
        return std::nullopt;
    }

    QFile manifestFile(dir.filePath(fileNameString(kManifestFileName)));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject manifest = document.object();
    HistoryEntry entry;
    entry.id = manifest.value(QStringLiteral("id")).toString(dir.dirName());
    const QString kind = manifest.value(QStringLiteral("kind")).toString();
    if (kind != QString::fromLatin1(kCaptureSessionKind)) {
        return std::nullopt;
    }
    entry.createdAt = QDateTime::fromString(
        manifest.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    entry.entryDirectory = entryDirectory;
    entry.previewPath = dir.filePath(manifest.value(QStringLiteral("previewPath")).toString());
    entry.resultPath = dir.filePath(manifest.value(QStringLiteral("resultPath")).toString());
    entry.replayAvailable = manifest.value(QStringLiteral("replayAvailable")).toBool(false);
    entry.resultSize = deserializeSize(manifest.value(QStringLiteral("resultSize")));
    entry.devicePixelRatio = manifest.value(QStringLiteral("devicePixelRatio")).toDouble(1.0);

    entry.canvasPath = dir.filePath(manifest.value(QStringLiteral("canvasPath")).toString());
    entry.annotationsPath = dir.filePath(manifest.value(QStringLiteral("annotationsPath")).toString());
    entry.selectionRect = deserializeRect(manifest.value(QStringLiteral("selectionRect")));
    entry.captureRegions = deserializeCaptureRegions(manifest.value(QStringLiteral("captureRegions")));
    entry.canvasLogicalSize = deserializeSize(manifest.value(QStringLiteral("canvasLogicalSize")));
    entry.cornerRadius = manifest.value(QStringLiteral("cornerRadius")).toInt();

    return entry;
}

} // namespace SnapTray
