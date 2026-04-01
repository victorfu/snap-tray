#include "qml/HistoryModel.h"

#include <QDate>
#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

namespace SnapTray {

namespace {

constexpr qint64 kLargeFileThresholdBytes = 2LL * 1024LL * 1024LL;

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

int HistoryModel::totalCount() const
{
    return m_allEntries.size();
}

int HistoryModel::filterCountsRevision() const
{
    return m_filterCountsRevision;
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
    case DisplayTitleRole:
        return displayTitle(entry);
    case RelativeDateTextRole:
        return relativeDateText(entry);
    case ImageSizeRole:
        return pixelSize(entry);
    case SizeTextRole:
        return sizeText(entry);
    case FileSizeBytesRole:
        return entry.fileSizeBytes;
    case FileSizeTextRole:
        return fileSizeText(entry);
    case ResolutionTextRole:
        return resolutionText(entry);
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
        {DisplayTitleRole, "displayTitle"},
        {RelativeDateTextRole, "relativeDateText"},
        {ImageSizeRole, "imageSize"},
        {SizeTextRole, "sizeText"},
        {FileSizeBytesRole, "fileSizeBytes"},
        {FileSizeTextRole, "fileSizeText"},
        {ResolutionTextRole, "resolutionText"},
        {TooltipTextRole, "tooltipText"},
    };
}

int HistoryModel::activeFilter() const
{
    return static_cast<int>(m_activeFilter);
}

HistoryModel::Filter HistoryModel::activeFilterEnum() const
{
    return m_activeFilter;
}

void HistoryModel::setActiveFilter(int filterValue)
{
    setActiveFilter(static_cast<Filter>(filterValue));
}

void HistoryModel::setActiveFilter(Filter filter)
{
    if (m_activeFilter == filter) {
        return;
    }

    beginResetModel();
    m_activeFilter = filter;
    rebuildVisibleEntries();
    endResetModel();

    emit activeFilterChanged();
    emit countChanged();
}

int HistoryModel::sortOrder() const
{
    return static_cast<int>(m_sortOrder);
}

HistoryModel::SortOrder HistoryModel::sortOrderEnum() const
{
    return m_sortOrder;
}

void HistoryModel::setSortOrder(int sortOrderValue)
{
    setSortOrder(static_cast<SortOrder>(sortOrderValue));
}

void HistoryModel::setSortOrder(SortOrder sortOrder)
{
    if (m_sortOrder == sortOrder) {
        return;
    }

    beginResetModel();
    m_sortOrder = sortOrder;
    rebuildVisibleEntries();
    endResetModel();

    emit sortOrderChanged();
    emit countChanged();
}

void HistoryModel::refresh()
{
    beginResetModel();
    m_allEntries = HistoryStore::loadEntries();
    rebuildVisibleEntries();
    endResetModel();

    emit countChanged();
    emit totalCountChanged();
    ++m_filterCountsRevision;
    emit filterCountsRevisionChanged();
}

HistoryEntry HistoryModel::entryAt(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return {};
    }
    return m_entries.at(index);
}

int HistoryModel::countForFilter(Filter filter) const
{
    return static_cast<int>(std::count_if(m_allEntries.begin(),
                                          m_allEntries.end(),
                                          [this, filter](const HistoryEntry& entry) {
        return matchesFilter(entry, filter);
    }));
}

int HistoryModel::indexOfId(const QString& id) const
{
    if (id.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QString HistoryModel::idAt(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return {};
    }

    return m_entries.at(index).id;
}

void HistoryModel::rebuildVisibleEntries()
{
    m_entries.clear();
    m_entries.reserve(m_allEntries.size());

    for (const auto& entry : m_allEntries) {
        if (matchesFilter(entry, m_activeFilter)) {
            m_entries.append(entry);
        }
    }

    std::sort(m_entries.begin(), m_entries.end(), [this](const HistoryEntry& lhs, const HistoryEntry& rhs) {
        switch (m_sortOrder) {
        case OldestFirst:
            if (lhs.createdAt != rhs.createdAt) {
                return lhs.createdAt < rhs.createdAt;
            }
            return lhs.id < rhs.id;
        case LargestFirst:
            if (lhs.fileSizeBytes != rhs.fileSizeBytes) {
                return lhs.fileSizeBytes > rhs.fileSizeBytes;
            }
            if (lhs.createdAt != rhs.createdAt) {
                return lhs.createdAt > rhs.createdAt;
            }
            return lhs.id > rhs.id;
        case NewestFirst:
        default:
            if (lhs.createdAt != rhs.createdAt) {
                return lhs.createdAt > rhs.createdAt;
            }
            return lhs.id > rhs.id;
        }
    });
}

bool HistoryModel::matchesFilter(const HistoryEntry& entry, Filter filter) const
{
    switch (filter) {
    case Last7Days:
        return entry.createdAt.isValid()
            && entry.createdAt.date() >= QDate::currentDate().addDays(-6);
    case LargeFiles:
        return entry.fileSizeBytes >= kLargeFileThresholdBytes;
    case Replayable:
        return entry.replayAvailable;
    case AllScreenshots:
    default:
        return true;
    }
}

QString HistoryModel::capturedAtText(const HistoryEntry& entry) const
{
    return entry.createdAt.isValid()
        ? entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : historyText("Unknown time");
}

QString HistoryModel::displayTitle(const HistoryEntry& entry) const
{
    if (!entry.createdAt.isValid()) {
        return historyText("Screenshot");
    }

    return historyText("Screenshot %1")
        .arg(entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

QString HistoryModel::relativeDateText(const HistoryEntry& entry) const
{
    if (!entry.createdAt.isValid()) {
        return historyText("Unknown time");
    }

    const int dayDelta = entry.createdAt.date().daysTo(QDate::currentDate());
    if (dayDelta <= 0) {
        return historyText("Today");
    }
    if (dayDelta == 1) {
        return historyText("Yesterday");
    }
    return historyText("%1 days ago").arg(dayDelta);
}

QSize HistoryModel::pixelSize(const HistoryEntry& entry) const
{
    return entry.resultSize;
}

QString HistoryModel::sizeText(const HistoryEntry& entry) const
{
    return resolutionText(entry);
}

QString HistoryModel::resolutionText(const HistoryEntry& entry) const
{
    const QSize size = pixelSize(entry);
    return size.isValid()
        ? historyText("%1 x %2").arg(size.width()).arg(size.height())
        : historyText("Unknown");
}

QString HistoryModel::fileSizeText(const HistoryEntry& entry) const
{
    if (entry.fileSizeBytes <= 0) {
        return historyText("Unknown size");
    }

    return QLocale().formattedDataSize(entry.fileSizeBytes,
                                       1,
                                       QLocale::DataSizeIecFormat);
}

QString HistoryModel::tooltipText(const HistoryEntry& entry) const
{
    return historyText("Time: %1\nResolution: %2\nFile size: %3")
        .arg(capturedAtText(entry), resolutionText(entry), fileSizeText(entry));
}

} // namespace SnapTray
