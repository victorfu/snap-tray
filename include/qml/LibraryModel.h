#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVector>

namespace SnapTray {

class HistoryModel;
class RecordingLibraryViewModel;

// The Library window's list (#226): screenshots from HistoryModel and
// editable recording projects from RecordingLibraryViewModel, mapped into one
// read-only list. Both storage models stay as they are; each row points back
// into its own source (sourceRowAt) so actions go to the right backend.
class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int screenshotCount READ screenshotCount NOTIFY countsChanged)
    Q_PROPERTY(int recordingCount READ recordingCount NOTIFY countsChanged)
    Q_PROPERTY(int allCount READ allCount NOTIFY countsChanged)
    // Bumped on every rebuild, for QML bindings that read roles by row.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Source { AllCaptures = 0, Screenshots, Recordings };
    Q_ENUM(Source)
    enum Kind { Screenshot = 0, Recording };
    Q_ENUM(Kind)
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        KindRole,
        KindLabelRole,
        TitleRole,
        ThumbnailUrlRole,
        MetaTextRole,
        DetailTextRole,
        BusyRole,
        SavedFileExistsRole,
        SourceRowRole,
    };

    LibraryModel(HistoryModel* history, RecordingLibraryViewModel* recordings, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int source() const { return m_source; }
    void setSource(int source);
    int count() const { return rowCount(); }
    int screenshotCount() const;
    int recordingCount() const;
    int allCount() const { return screenshotCount() + recordingCount(); }
    int revision() const { return m_revision; }

    Q_INVOKABLE QString keyAt(int row) const;
    Q_INVOKABLE int kindAt(int row) const;
    Q_INVOKABLE int sourceRowAt(int row) const;
    Q_INVOKABLE int indexOfKey(const QString& key) const;
    // A role by its QML name ("busy", "savedFileExists", ...).
    Q_INVOKABLE QVariant roleAt(int row, const QString& roleName) const;

signals:
    void sourceChanged();
    void countChanged();
    void countsChanged();
    void revisionChanged();

private:
    struct Row {
        Kind kind = Screenshot;
        int sourceRow = -1;
        QString key;
        QDateTime time;
        qint64 bytes = 0;
    };
    void rebuild();

    QPointer<HistoryModel> m_history;
    QPointer<RecordingLibraryViewModel> m_recordings;
    int m_source = AllCaptures;
    QVector<Row> m_rows;
    int m_revision = 0;
};

} // namespace SnapTray
