#pragma once

#include "recording/RecordingSession.h"

#include <QObject>
#include <QSet>
#include <QVariantList>

namespace SnapTray {

// Recordings an earlier run left behind, for the recovery dialog.
class RecordingRecoveryViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY recordingsChanged)

public:
    explicit RecordingRecoveryViewModel(QObject* parent = nullptr);

    void setRecoveries(const QList<RecordingSession::Recoverable>& recoveries);
    QVariantList recordings() const { return m_rows; }
    int count() const { return m_items.size(); }

    // Opens the recording in the preview (the dialog closes on success).
    Q_INVOKABLE void recover(int index);
    // Deletes the recording, its window timeline and its manifest, unless
    // another instance holds its session lock (the row is then marked busy).
    Q_INVOKABLE void discard(int index);
    Q_INVOKABLE void later();

    // Plain-language summaries, also used for the toast.
    static QString durationText(qint64 ms);
    static QString statusText(const RecordingSession::Recoverable& item);

signals:
    void recordingsChanged();
    void recoverRequested(const QString& videoPath);
    void discarded(const QString& videoPath);
    void closeRequested();

private:
    void rebuildRows();

    QList<RecordingSession::Recoverable> m_items;
    QVariantList m_rows;
    QSet<QString> m_busy;
};

} // namespace SnapTray
