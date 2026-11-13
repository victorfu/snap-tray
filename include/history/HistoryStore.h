#pragma once

#include "beautify/BeautifySettings.h"
#include "region/MultiRegionManager.h"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QtGlobal>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>
#include <optional>

namespace SnapTray {

struct HistoryEntry
{
    QString id;
    QDateTime createdAt;
    QString entryDirectory;
    QString previewPath;
    QString resultPath;
    bool replayAvailable = true;
    QSize resultSize;
    qint64 fileSizeBytes = 0;
    qreal devicePixelRatio = 1.0;

    QString canvasPath;
    QRect selectionRect;
    QVector<MultiRegionManager::Region> captureRegions;
    QString annotationsPath;
    QSize canvasLogicalSize;
    int cornerRadius = 0;
    QString windowTitle;
    QString ownerApp;

    // Manifest format the entry was written in; manifests from before #344
    // carry no number and count as 1. An entry from a newer SnapTray is listed
    // (its finished picture can still be pinned, copied and saved) but is not
    // replayable, because this version cannot tell what its document means.
    int schemaVersion = 1;
    bool isFromNewerVersion() const;

    // Set when resultPath is canvas + annotations with Beautify applied: the
    // exact settings used, so reopening the entry continues from them instead
    // of from the global defaults or a saved style that may have changed.
    std::optional<BeautifySettings> beautify;
};

struct CaptureSessionWriteRequest
{
    // Empty: the store picks one. Callers that must refer to the entry before
    // it is written take one from HistoryStore::newEntryId().
    QString id;
    QImage canvasImage;
    QImage resultImage;
    QRect selectionRect;
    QVector<MultiRegionManager::Region> captureRegions;
    QByteArray annotationsJson;
    qreal devicePixelRatio = 1.0;
    QSize canvasLogicalSize;
    int cornerRadius = 0;
    int maxEntries = 20;
    QDateTime createdAt = QDateTime::currentDateTime();
    QString windowTitle;
    QString ownerApp;

    // Beautify settings resultImage was rendered with, if any.
    std::optional<BeautifySettings> beautify;
    // An existing entry whose document (canvas, annotations, selection and
    // capture context) the new entry takes over, so the annotations stay
    // editable. When that entry is gone or not replayable, the fields above
    // are used instead; they must always describe a complete document.
    QString documentSourceEntryId;
};

class HistoryStore
{
public:
    // The manifest format this version writes.
    static constexpr int kSchemaVersion = 2;

    static QString historyRootPath();
    static QString entriesRootPath();
    static QList<HistoryEntry> loadEntries();
    static std::optional<HistoryEntry> loadEntry(const QString& id);
    static std::optional<HistoryEntry> writeCaptureSession(const CaptureSessionWriteRequest& request);
    static bool deleteEntry(const HistoryEntry& entry);
    static QString newEntryId(const QDateTime& createdAt);

    // Test hook: called with each step of a write ("canvas.png", "result.png",
    // "annotations.json", "manifest.json", then "commit"); returning true makes
    // that step fail as a full disk or a lost volume would.
    static void setWriteFaultForTests(std::function<bool(const QString& step)> fault);

private:
    static std::optional<HistoryEntry> loadEntryFromDirectory(const QString& entryDirectory);
};

} // namespace SnapTray
