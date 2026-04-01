#pragma once

#include "history/HistoryStore.h"

#include <QAbstractListModel>
#include <QObject>
#include <QUrl>

namespace SnapTray {

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int filterCountsRevision READ filterCountsRevision NOTIFY filterCountsRevisionChanged)
    Q_PROPERTY(int activeFilter READ activeFilter WRITE setActiveFilter NOTIFY activeFilterChanged)
    Q_PROPERTY(int sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)

public:
    enum Filter {
        AllScreenshots = 0,
        Last7Days,
        LargeFiles,
        Replayable,
    };
    Q_ENUM(Filter)

    enum SortOrder {
        NewestFirst = 0,
        OldestFirst,
        LargestFirst,
    };
    Q_ENUM(SortOrder)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        ReplayAvailableRole,
        CanEditRole,
        PreviewPathRole,
        ThumbnailUrlRole,
        CapturedAtRole,
        CapturedAtTextRole,
        DisplayTitleRole,
        RelativeDateTextRole,
        ImageSizeRole,
        SizeTextRole,
        FileSizeBytesRole,
        FileSizeTextRole,
        ResolutionTextRole,
        TooltipTextRole,
    };

    explicit HistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int totalCount() const;
    int filterCountsRevision() const;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeFilter() const;
    Filter activeFilterEnum() const;
    void setActiveFilter(int filterValue);
    void setActiveFilter(Filter filter);

    int sortOrder() const;
    SortOrder sortOrderEnum() const;
    void setSortOrder(int sortOrderValue);
    void setSortOrder(SortOrder sortOrder);

    void refresh();
    HistoryEntry entryAt(int index) const;

    Q_INVOKABLE int countForFilter(Filter filter) const;
    Q_INVOKABLE int indexOfId(const QString& id) const;
    Q_INVOKABLE QString idAt(int index) const;

signals:
    void countChanged();
    void totalCountChanged();
    void filterCountsRevisionChanged();
    void activeFilterChanged();
    void sortOrderChanged();

private:
    void rebuildVisibleEntries();
    bool matchesFilter(const HistoryEntry& entry, Filter filter) const;
    QString capturedAtText(const HistoryEntry& entry) const;
    QString displayTitle(const HistoryEntry& entry) const;
    QString relativeDateText(const HistoryEntry& entry) const;
    QString sizeText(const HistoryEntry& entry) const;
    QString resolutionText(const HistoryEntry& entry) const;
    QString fileSizeText(const HistoryEntry& entry) const;
    QString tooltipText(const HistoryEntry& entry) const;
    QSize pixelSize(const HistoryEntry& entry) const;

    QList<HistoryEntry> m_allEntries;
    QList<HistoryEntry> m_entries;
    int m_filterCountsRevision = 0;
    Filter m_activeFilter = AllScreenshots;
    SortOrder m_sortOrder = NewestFirst;
};

} // namespace SnapTray
