#include "qml/PinHistoryModel.h"

#include <QCoreApplication>
#include <QDateTime>

namespace SnapTray {

namespace {

QString pinHistoryText(const char* sourceText)
{
    return QCoreApplication::translate("PinHistoryWindow", sourceText);
}

} // namespace

PinHistoryModel::PinHistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
    refresh();
}

int PinHistoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant PinHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return QVariant();
    }

    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case ImagePathRole:
        return entry.imagePath;
    case ThumbnailUrlRole:
        return QUrl::fromLocalFile(entry.imagePath);
    case MetadataPathRole:
        return entry.metadataPath;
    case DprMetadataPathRole:
        return entry.dprMetadataPath;
    case CapturedAtRole:
        return entry.capturedAt;
    case CapturedAtTextRole:
        return capturedAtText(entry);
    case ImageSizeRole:
        return entry.imageSize;
    case SizeTextRole:
        return sizeText(entry);
    case DevicePixelRatioRole:
        return entry.devicePixelRatio;
    case TooltipTextRole:
        return tooltipText(entry);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> PinHistoryModel::roleNames() const
{
    return {
        {ImagePathRole, "imagePath"},
        {ThumbnailUrlRole, "thumbnailUrl"},
        {MetadataPathRole, "metadataPath"},
        {DprMetadataPathRole, "dprMetadataPath"},
        {CapturedAtRole, "capturedAt"},
        {CapturedAtTextRole, "capturedAtText"},
        {ImageSizeRole, "imageSize"},
        {SizeTextRole, "sizeText"},
        {DevicePixelRatioRole, "devicePixelRatio"},
        {TooltipTextRole, "tooltipText"},
    };
}

void PinHistoryModel::refresh()
{
    beginResetModel();
    m_entries = PinHistoryStore::loadEntries();
    endResetModel();
    emit countChanged();
}

PinHistoryEntry PinHistoryModel::entryAt(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return {};
    }
    return m_entries.at(index);
}

QString PinHistoryModel::capturedAtText(const PinHistoryEntry& entry) const
{
    return entry.capturedAt.isValid()
        ? entry.capturedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : pinHistoryText("Unknown time");
}

QString PinHistoryModel::sizeText(const PinHistoryEntry& entry) const
{
    return entry.imageSize.isValid()
        ? pinHistoryText("%1 x %2").arg(entry.imageSize.width()).arg(entry.imageSize.height())
        : pinHistoryText("Unknown");
}

QString PinHistoryModel::tooltipText(const PinHistoryEntry& entry) const
{
    const QString timeText = entry.capturedAt.isValid()
        ? entry.capturedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : pinHistoryText("Unknown");
    const QString sizeValue = entry.imageSize.isValid()
        ? pinHistoryText("%1 x %2").arg(entry.imageSize.width()).arg(entry.imageSize.height())
        : pinHistoryText("Unknown");

    return pinHistoryText("Time: %1\nSize: %2").arg(timeText, sizeValue);
}

} // namespace SnapTray
