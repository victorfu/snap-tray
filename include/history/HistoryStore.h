#pragma once

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
};

struct CaptureSessionWriteRequest
{
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
};

class HistoryStore
{
public:
    static QString historyRootPath();
    static QString entriesRootPath();
    static QList<HistoryEntry> loadEntries();
    static std::optional<HistoryEntry> loadEntry(const QString& id);
    static std::optional<HistoryEntry> writeCaptureSession(const CaptureSessionWriteRequest& request);
    static bool deleteEntry(const HistoryEntry& entry);

private:
    static std::optional<HistoryEntry> loadEntryFromDirectory(const QString& entryDirectory);
};

} // namespace SnapTray
