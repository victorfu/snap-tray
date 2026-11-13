#include "history/HistoryStore.h"

#include "beautify/BeautifySettingsJson.h"
#include "utils/ImageSaveUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
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
// An entry is built in a staging directory and only becomes an entry when the
// whole directory is renamed, so a write that fails or is cut short never
// leaves a picture next to a document that does not belong to it.
constexpr auto kStagingPrefix = ".staging-";
constexpr auto kCommitStep = "commit";

// One write at a time: a write also clears staging directories left by an
// interrupted one, which must not be another thread's write in progress.
// Both are never destroyed: HistoryRecorder finishes queued writes while the
// process exits, after ordinary statics may already be gone.
QMutex* writeMutex()
{
    static auto* mutex = new QMutex;
    return mutex;
}

std::function<bool(const QString&)>& writeFault()
{
    static auto* fault = new std::function<bool(const QString&)>;
    return *fault;
}

bool stepFails(const char* step)
{
    return writeFault() && writeFault()(QString::fromLatin1(step));
}

// On Windows a directory cannot be renamed while another process (a virus
// scanner, the search indexer) has one of its new files open for a moment.
constexpr int kCommitAttempts = 6;
constexpr int kCommitRetryDelayMs = 50;

bool commitStagingDirectory(QDir& root, const QString& stagingName, const QString& id)
{
    for (int attempt = 0; attempt < kCommitAttempts; ++attempt) {
        if (attempt > 0) {
            QThread::msleep(kCommitRetryDelayMs);
        }
        if (root.rename(stagingName, id)) {
            return true;
        }
    }
    return false;
}

bool isStagingName(const QString& directoryName)
{
    return directoryName.startsWith(QLatin1String(kStagingPrefix));
}

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

QString fileNameString(const char* value)
{
    return QString::fromLatin1(value);
}

bool writeTextFile(const QDir& directory, const char* fileName, const QByteArray& data)
{
    if (stepFails(fileName)) {
        return false;
    }
    // QSaveFile reports a failed flush, which a plain QFile only shows on close.
    QSaveFile file(directory.filePath(fileNameString(fileName)));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool saveImage(const QImage& image, const QDir& directory, const char* fileName)
{
    if (image.isNull() || stepFails(fileName)) {
        return false;
    }
    const QString filePath = directory.filePath(fileNameString(fileName));

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

bool copyFile(const QString& sourcePath, const QDir& directory, const char* fileName)
{
    if (stepFails(fileName)) {
        return false;
    }
    const QString targetPath = directory.filePath(fileNameString(fileName));
    if (QFile::copy(sourcePath, targetPath)) {
        return true;
    }
    QFile::remove(targetPath);
    return false;
}

void removeStaleStagingDirectories(const QDir& root)
{
    const QStringList names = root.entryList(
        {QString::fromLatin1(kStagingPrefix) + QLatin1Char('*')},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString& name : names) {
        QDir(root.filePath(name)).removeRecursively();
    }
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

bool HistoryEntry::isFromNewerVersion() const
{
    return schemaVersion > HistoryStore::kSchemaVersion;
}

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
        if (isStagingName(dirInfo.fileName())) {
            continue;
        }
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

QString HistoryStore::newEntryId(const QDateTime& createdAt)
{
    return QStringLiteral("capture_%1_%2")
        .arg(createdAt.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void HistoryStore::setWriteFaultForTests(std::function<bool(const QString& step)> fault)
{
    QMutexLocker locker(writeMutex());
    writeFault() = std::move(fault);
}

std::optional<HistoryEntry> HistoryStore::writeCaptureSession(const CaptureSessionWriteRequest& request)
{
    if (request.canvasImage.isNull() || request.resultImage.isNull() || !request.selectionRect.isValid()) {
        return std::nullopt;
    }

    QMutexLocker locker(writeMutex());

    QDir root(entriesRootPath());
    if (!root.exists() && !root.mkpath(QStringLiteral("."))) {
        return std::nullopt;
    }
    removeStaleStagingDirectories(root);

    const QString id = request.id.isEmpty() ? newEntryId(request.createdAt) : request.id;
    if (isStagingName(id) || QFileInfo(id).fileName() != id || root.exists(id)) {
        // An entry is never rewritten in place.
        return std::nullopt;
    }

    const QString stagingName = QString::fromLatin1(kStagingPrefix) + id;
    if (!root.mkpath(stagingName)) {
        return std::nullopt;
    }
    const QDir stagingDir(root.filePath(stagingName));
    const auto abandon = [&stagingDir]() {
        QDir(stagingDir.path()).removeRecursively();
        return std::nullopt;
    };

    // The document: taken over from an existing entry, or the request's own.
    QRect selectionRect = request.selectionRect;
    QVector<MultiRegionManager::Region> captureRegions = request.captureRegions;
    qreal devicePixelRatio = request.devicePixelRatio;
    QSize canvasLogicalSize = request.canvasLogicalSize;
    int cornerRadius = request.cornerRadius;
    QString windowTitle = request.windowTitle;
    QString ownerApp = request.ownerApp;

    bool documentWritten = false;
    if (!request.documentSourceEntryId.isEmpty()) {
        const auto source = loadEntry(request.documentSourceEntryId);
        if (source && source->replayAvailable &&
            copyFile(source->canvasPath, stagingDir, kCanvasFileName) &&
            copyFile(source->annotationsPath, stagingDir, kAnnotationsFileName)) {
            selectionRect = source->selectionRect;
            captureRegions = source->captureRegions;
            devicePixelRatio = source->devicePixelRatio;
            canvasLogicalSize = source->canvasLogicalSize;
            cornerRadius = source->cornerRadius;
            windowTitle = source->windowTitle;
            ownerApp = source->ownerApp;
            documentWritten = true;
        } else {
            QFile::remove(stagingDir.filePath(fileNameString(kCanvasFileName)));
            QFile::remove(stagingDir.filePath(fileNameString(kAnnotationsFileName)));
        }
    }
    if (!documentWritten &&
        (!saveImage(request.canvasImage, stagingDir, kCanvasFileName) ||
         !writeTextFile(stagingDir, kAnnotationsFileName, request.annotationsJson))) {
        return abandon();
    }
    if (!saveImage(request.resultImage, stagingDir, kResultFileName)) {
        return abandon();
    }

    QJsonObject manifest = baseManifest(id,
                                        request.createdAt,
                                        fileNameString(kResultFileName),
                                        fileNameString(kResultFileName),
                                        true);
    manifest.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    manifest.insert(QStringLiteral("canvasPath"), fileNameString(kCanvasFileName));
    manifest.insert(QStringLiteral("annotationsPath"), fileNameString(kAnnotationsFileName));
    manifest.insert(QStringLiteral("selectionRect"), serializeRect(selectionRect));
    manifest.insert(QStringLiteral("captureRegions"), serializeCaptureRegions(captureRegions));
    manifest.insert(QStringLiteral("devicePixelRatio"), devicePixelRatio);
    manifest.insert(QStringLiteral("canvasLogicalSize"), serializeSize(canvasLogicalSize));
    manifest.insert(QStringLiteral("resultSize"), serializeSize(request.resultImage.size()));
    manifest.insert(QStringLiteral("cornerRadius"), cornerRadius);
    manifest.insert(QStringLiteral("windowTitle"), windowTitle);
    manifest.insert(QStringLiteral("ownerApp"), ownerApp);
    if (request.beautify) {
        manifest.insert(QStringLiteral("beautify"), beautifySettingsToJson(*request.beautify));
    }

    if (!writeTextFile(stagingDir, kManifestFileName,
                       QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
        return abandon();
    }

    // The commit: until this rename the entry does not exist for any reader.
    if (stepFails(kCommitStep) || !commitStagingDirectory(root, stagingName, id)) {
        return abandon();
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
    if (!dir.exists() || isStagingName(dir.dirName())) {
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
    entry.fileSizeBytes = QFileInfo(entry.resultPath).size();
    entry.devicePixelRatio = manifest.value(QStringLiteral("devicePixelRatio")).toDouble(1.0);

    entry.canvasPath = dir.filePath(manifest.value(QStringLiteral("canvasPath")).toString());
    entry.annotationsPath = dir.filePath(manifest.value(QStringLiteral("annotationsPath")).toString());
    entry.selectionRect = deserializeRect(manifest.value(QStringLiteral("selectionRect")));
    entry.captureRegions = deserializeCaptureRegions(manifest.value(QStringLiteral("captureRegions")));
    entry.canvasLogicalSize = deserializeSize(manifest.value(QStringLiteral("canvasLogicalSize")));
    entry.cornerRadius = manifest.value(QStringLiteral("cornerRadius")).toInt();
    entry.windowTitle = manifest.value(QStringLiteral("windowTitle")).toString();
    entry.ownerApp = manifest.value(QStringLiteral("ownerApp")).toString();

    // Anything but a whole number is not a version this code knows: treat it
    // as newer rather than as a manifest from before versions existed.
    const QJsonValue schemaVersion = manifest.value(QStringLiteral("schemaVersion"));
    entry.schemaVersion = schemaVersion.isUndefined()
        ? 1
        : schemaVersion.toInt(HistoryStore::kSchemaVersion + 1);
    if (entry.schemaVersion < 1) {
        entry.schemaVersion = HistoryStore::kSchemaVersion + 1;
    }
    if (entry.isFromNewerVersion()) {
        entry.replayAvailable = false;
        return entry;
    }

    const QJsonValue beautify = manifest.value(QStringLiteral("beautify"));
    if (!beautify.isUndefined()) {
        entry.beautify = beautifySettingsFromJson(beautify.toObject());
        if (!entry.beautify) {
            // Replaying it would show the picture without the look it was
            // saved with, or with today's defaults; the finished picture stays.
            qWarning() << "HistoryStore: unreadable beautify settings in" << entryDirectory;
            entry.replayAvailable = false;
        }
    }

    return entry;
}

} // namespace SnapTray
