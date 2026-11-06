#include "qml/LibraryModel.h"

#include "qml/HistoryModel.h"
#include "qml/RecordingLibraryViewModel.h"
#include "qml/RecordingRecoveryViewModel.h"

#include <QFileInfo>
#include <QLocale>
#include <QUrl>

#include <algorithm>

namespace SnapTray {

LibraryModel::LibraryModel(HistoryModel* history, RecordingLibraryViewModel* recordings, QObject* parent)
    : QAbstractListModel(parent)
    , m_history(history)
    , m_recordings(recordings)
{
    if (m_history) {
        // HistoryModel resets on every refresh, filter and sort change.
        connect(m_history, &QAbstractItemModel::modelReset, this, &LibraryModel::rebuild);
        connect(m_history, &QAbstractItemModel::rowsInserted, this, &LibraryModel::rebuild);
        connect(m_history, &QAbstractItemModel::rowsRemoved, this, &LibraryModel::rebuild);
        connect(m_history, &QAbstractItemModel::dataChanged, this, &LibraryModel::rebuild);
        connect(m_history, &HistoryModel::totalCountChanged, this, &LibraryModel::countsChanged);
    }
    if (m_recordings)
        connect(m_recordings, &RecordingLibraryViewModel::projectsChanged, this, [this] {
            rebuild();
            emit countsChanged();
        });
    rebuild();
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int LibraryModel::screenshotCount() const
{
    return m_history ? m_history->totalCount() : 0;
}

int LibraryModel::recordingCount() const
{
    return m_recordings ? m_recordings->count() : 0;
}

void LibraryModel::setSource(int source)
{
    source = qBound(int(AllCaptures), source, int(Recordings));
    if (m_source == source) return;
    m_source = source;
    rebuild();
    emit sourceChanged();
}

void LibraryModel::rebuild()
{
    const int before = m_rows.size();
    beginResetModel();
    m_rows.clear();
    if (m_history && m_source != Recordings) {
        for (int i = 0; i < m_history->rowCount(); ++i) {
            const QModelIndex index = m_history->index(i);
            Row row;
            row.kind = Screenshot;
            row.sourceRow = i;
            row.key = m_history->data(index, HistoryModel::IdRole).toString();
            row.time = m_history->data(index, HistoryModel::CapturedAtRole).toDateTime();
            row.bytes = m_history->data(index, HistoryModel::FileSizeBytesRole).toLongLong();
            m_rows.append(row);
        }
    }
    if (m_recordings && m_source != Screenshots) {
        QVector<Row> projects;
        const auto& items = m_recordings->items();
        for (int i = 0; i < items.size(); ++i) {
            Row row;
            row.kind = Recording;
            row.sourceRow = i;
            row.key = items.at(i).dir;
            row.time = items.at(i).created;
            row.bytes = items.at(i).sizeBytes();
            projects.append(row);
        }
        m_rows += projects;
    }
    // Screenshots arrive in HistoryModel's order; the merge applies the same
    // order to both kinds. Stable, so equal keys keep HistoryModel's order.
    const int order = m_history ? m_history->sortOrder() : int(HistoryModel::NewestFirst);
    std::stable_sort(m_rows.begin(), m_rows.end(), [order](const Row& a, const Row& b) {
        switch (order) {
        case HistoryModel::OldestFirst: return a.time < b.time;
        case HistoryModel::LargestFirst: return a.bytes > b.bytes;
        default: return a.time > b.time;
        }
    });
    endResetModel();
    ++m_revision;
    emit revisionChanged();
    if (before != m_rows.size()) emit countChanged();
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case KeyRole: return row.key;
    case KindRole: return int(row.kind);
    case SourceRowRole: return row.sourceRow;
    case KindLabelRole: return row.kind == Screenshot ? tr("Screenshot") : tr("Editable project");
    default: break;
    }

    if (row.kind == Screenshot) {
        if (!m_history) return {};
        const QModelIndex source = m_history->index(row.sourceRow);
        switch (role) {
        case TitleRole: return m_history->data(source, HistoryModel::DisplayTitleRole);
        case ThumbnailUrlRole: return m_history->data(source, HistoryModel::ThumbnailUrlRole);
        case MetaTextRole:
            return QStringLiteral("%1 · %2 · %3")
                .arg(m_history->data(source, HistoryModel::RelativeDateTextRole).toString(),
                     m_history->data(source, HistoryModel::FileSizeTextRole).toString(),
                     m_history->data(source, HistoryModel::ResolutionTextRole).toString());
        case DetailTextRole: return m_history->data(source, HistoryModel::TooltipTextRole);
        case BusyRole: return false;
        case SavedFileExistsRole: return false;
        default: return {};
        }
    }

    if (!m_recordings || row.sourceRow >= m_recordings->items().size()) return {};
    const RecordingProject& project = m_recordings->items().at(row.sourceRow);
    const bool busy = !m_recordings->openProjectDir().isEmpty() && project.dir == m_recordings->openProjectDir();
    const bool savedExists = !project.savedPath.isEmpty() && QFileInfo::exists(project.savedPath);
    switch (role) {
    case TitleRole: return project.title();
    case ThumbnailUrlRole:
        return QFileInfo::exists(project.thumbnailPath())
            ? QUrl::fromLocalFile(project.thumbnailPath()).toString()
            : QString();
    case MetaTextRole:
        return QStringLiteral("%1 · %2 · %3")
            .arg(HistoryModel::relativeDateFor(project.created),
                 RecordingRecoveryViewModel::durationText(project.durationMs),
                 QLocale().formattedDataSize(row.bytes));
    case DetailTextRole:
        if (busy) return tr("Open in the recording preview");
        if (project.savedPath.isEmpty()) return tr("Not saved yet");
        return savedExists ? tr("Saved as %1").arg(QFileInfo(project.savedPath).fileName())
                           : tr("Saved file was moved or deleted");
    case BusyRole: return busy;
    case SavedFileExistsRole: return savedExists;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryModel::roleNames() const
{
    return {
        {KeyRole, "key"},
        {KindRole, "kind"},
        {KindLabelRole, "kindLabel"},
        {TitleRole, "title"},
        {ThumbnailUrlRole, "thumbnailUrl"},
        {MetaTextRole, "metaText"},
        {DetailTextRole, "detailText"},
        {BusyRole, "busy"},
        {SavedFileExistsRole, "savedFileExists"},
        {SourceRowRole, "sourceRow"},
    };
}

QString LibraryModel::keyAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).key : QString();
}

int LibraryModel::kindAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? int(m_rows.at(row).kind) : -1;
}

int LibraryModel::sourceRowAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).sourceRow : -1;
}

QVariant LibraryModel::roleAt(int row, const QString& roleName) const
{
    const QByteArray name = roleName.toUtf8();
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == name) return data(index(row), it.key());
    return {};
}

int LibraryModel::indexOfKey(const QString& key) const
{
    if (key.isEmpty()) return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).key == key) return i;
    return -1;
}

} // namespace SnapTray
