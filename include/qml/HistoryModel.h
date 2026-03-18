#pragma once

#include "history/HistoryStore.h"

#include <QAbstractListModel>
#include <QUrl>

namespace SnapTray {

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ReplayAvailableRole,
        CanEditRole,
        PreviewPathRole,
        ThumbnailUrlRole,
        CapturedAtRole,
        CapturedAtTextRole,
        ImageSizeRole,
        SizeTextRole,
        TooltipTextRole,
    };

    explicit HistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();
    HistoryEntry entryAt(int index) const;

signals:
    void countChanged();

private:
    QString capturedAtText(const HistoryEntry& entry) const;
    QString sizeText(const HistoryEntry& entry) const;
    QString tooltipText(const HistoryEntry& entry) const;
    QSize logicalSize(const HistoryEntry& entry) const;

    QList<HistoryEntry> m_entries;
};

} // namespace SnapTray
