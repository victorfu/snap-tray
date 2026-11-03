#include "qml/RecordingRecoveryViewModel.h"

#include <QLocale>
#include <QLockFile>
#include <QVariantMap>

namespace SnapTray {

namespace {
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kSecondsPerMinute = 60;
}

RecordingRecoveryViewModel::RecordingRecoveryViewModel(QObject* parent)
    : QObject(parent)
{
}

void RecordingRecoveryViewModel::setRecoveries(const QList<RecordingSession::Recoverable>& recoveries)
{
    m_items = recoveries;
    m_busy.clear();
    rebuildRows();
}

QString RecordingRecoveryViewModel::durationText(qint64 ms)
{
    const qint64 seconds = qMax<qint64>(0, ms) / kMsPerSecond;
    return QStringLiteral("%1:%2").arg(seconds / kSecondsPerMinute)
        .arg(seconds % kSecondsPerMinute, 2, 10, QLatin1Char('0'));
}

QString RecordingRecoveryViewModel::statusText(const RecordingSession::Recoverable& item)
{
    if (!item.decodable) {
        return tr("Cannot be recovered: the recording was interrupted before any playable part was saved.");
    }
    QString text = item.complete
        ? tr("Complete, %1 long").arg(durationText(item.durationMs))
        : tr("Interrupted: %1 can be recovered").arg(durationText(item.durationMs));
    // Never present a recording without its required audio as complete.
    if (item.audioMissing) {
        text += QStringLiteral(" \u00B7 ") + tr("audio is missing");
    }
    // An area chosen before recording (#194): the file is still the whole screen.
    if (!item.info.initialCrop.isEmpty()) {
        text += QStringLiteral(" \u00B7 ") + tr("the whole screen was recorded; it opens cropped to the chosen area");
    }
    return text;
}

void RecordingRecoveryViewModel::rebuildRows()
{
    m_rows.clear();
    for (const auto& item : m_items) {
        QVariantMap row;
        const QDateTime when = item.info.startedAt.isValid() ? item.info.startedAt.toLocalTime()
                                                             : item.info.updatedAt.toLocalTime();
        row[QStringLiteral("title")] = when.isValid()
            ? QLocale().toString(when, QLocale::ShortFormat)
            : tr("Unknown date");
        const bool busy = m_busy.contains(item.info.videoPath);
        row[QStringLiteral("status")] = busy
            ? tr("Open in another SnapTray window. Close it there, then try again.")
            : statusText(item);
        row[QStringLiteral("canRecover")] = item.decodable;
        row[QStringLiteral("busy")] = busy;
        m_rows.append(row);
    }
    emit recordingsChanged();
}

void RecordingRecoveryViewModel::recover(int index)
{
    if (index < 0 || index >= m_items.size() || !m_items.at(index).decodable) {
        return;
    }
    emit recoverRequested(m_items.at(index).info.videoPath);
}

void RecordingRecoveryViewModel::discard(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    const QString path = m_items.at(index).info.videoPath;
    // Another instance may have recovered this recording since the list was
    // built; its session lock keeps the files it has open.
    QLockFile lock(RecordingSession::lockPathFor(path));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        m_busy.insert(path);
        rebuildRows();
        return;
    }
    RecordingSession::discard(path);
    lock.unlock();
    m_busy.remove(path);
    m_items.removeAt(index);
    rebuildRows();
    emit discarded(path);
    if (m_items.isEmpty()) {
        emit closeRequested();
    }
}

void RecordingRecoveryViewModel::later()
{
    emit closeRequested();
}

} // namespace SnapTray
