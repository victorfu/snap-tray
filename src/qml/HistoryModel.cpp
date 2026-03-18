#include "qml/HistoryModel.h"

#include <QCoreApplication>

namespace SnapTray {

namespace {

QString historyText(const char* sourceText)
{
    return QCoreApplication::translate("HistoryWindow", sourceText);
}

} // namespace

HistoryModel::HistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
    refresh();
}

int HistoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case ReplayAvailableRole:
        return entry.replayAvailable;
    case CanEditRole:
        return true;
    case PreviewPathRole:
        return entry.previewPath;
    case ThumbnailUrlRole:
        return QUrl::fromLocalFile(entry.previewPath);
    case CapturedAtRole:
        return entry.createdAt;
    case CapturedAtTextRole:
        return capturedAtText(entry);
    case ImageSizeRole:
        return logicalSize(entry);
    case SizeTextRole:
        return sizeText(entry);
    case TooltipTextRole:
        return tooltipText(entry);
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {ReplayAvailableRole, "replayAvailable"},
        {CanEditRole, "canEdit"},
        {PreviewPathRole, "previewPath"},
        {ThumbnailUrlRole, "thumbnailUrl"},
        {CapturedAtRole, "capturedAt"},
        {CapturedAtTextRole, "capturedAtText"},
        {ImageSizeRole, "imageSize"},
        {SizeTextRole, "sizeText"},
        {TooltipTextRole, "tooltipText"},
    };
}

void HistoryModel::refresh()
{
    beginResetModel();
    m_entries = HistoryStore::loadEntries();
    endResetModel();
    emit countChanged();
}

HistoryEntry HistoryModel::entryAt(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return {};
    }
    return m_entries.at(index);
}

QString HistoryModel::capturedAtText(const HistoryEntry& entry) const
{
    return entry.createdAt.isValid()
        ? entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : historyText("Unknown time");
}

QSize HistoryModel::logicalSize(const HistoryEntry& entry) const
{
    if (!entry.resultSize.isValid()) {
        return {};
    }
    const qreal dpr = entry.devicePixelRatio > 0.0 ? entry.devicePixelRatio : 1.0;
    return QSize(qRound(entry.resultSize.width() / dpr),
                 qRound(entry.resultSize.height() / dpr));
}

QString HistoryModel::sizeText(const HistoryEntry& entry) const
{
    const QSize size = logicalSize(entry);
    return size.isValid()
        ? historyText("%1 x %2").arg(size.width()).arg(size.height())
        : historyText("Unknown");
}

QString HistoryModel::tooltipText(const HistoryEntry& entry) const
{
    return historyText("Time: %1\nSize: %2")
        .arg(capturedAtText(entry), sizeText(entry));
}

} // namespace SnapTray
