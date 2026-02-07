#ifndef PINHISTORYSTORE_H
#define PINHISTORYSTORE_H

#include <QDateTime>
#include <QList>
#include <QSize>
#include <QString>
#include <QtGlobal>

struct PinHistoryEntry
{
    QString imagePath;
    QString metadataPath;
    QString dprMetadataPath;
    QDateTime capturedAt;
    QSize imageSize;
    qreal devicePixelRatio = 1.0;
};

class PinHistoryStore
{
public:
    static QString historyFolderPath();
    static QList<PinHistoryEntry> loadEntries();
    static QList<PinHistoryEntry> loadEntriesFromFolder(const QString& folderPath);
    static QString regionMetadataPathForImage(const QString& imagePath);
    static QString dprMetadataPathForImage(const QString& imagePath);
    static bool deleteEntry(const PinHistoryEntry& entry);

private:
    static QDateTime parseCapturedAt(const QString& fileName, const QDateTime& fallbackTime);
    static qreal parseDevicePixelRatio(const QString& dprMetadataPath);
};

#endif // PINHISTORYSTORE_H
