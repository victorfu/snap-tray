#pragma once

#include "pinwindow/PinHistoryStore.h"

#include <QAbstractListModel>
#include <QUrl>

namespace SnapTray {

class PinHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        ImagePathRole = Qt::UserRole + 1,
        ThumbnailUrlRole,
        MetadataPathRole,
        DprMetadataPathRole,
        CapturedAtRole,
        CapturedAtTextRole,
        ImageSizeRole,
        SizeTextRole,
        DevicePixelRatioRole,
        TooltipTextRole,
    };

    explicit PinHistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();
    PinHistoryEntry entryAt(int index) const;

signals:
    void countChanged();

private:
    QString capturedAtText(const PinHistoryEntry& entry) const;
    QString sizeText(const PinHistoryEntry& entry) const;
    QString tooltipText(const PinHistoryEntry& entry) const;

    QList<PinHistoryEntry> m_entries;
};

} // namespace SnapTray
