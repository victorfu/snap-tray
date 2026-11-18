#include "qml/RecordingLibraryViewModel.h"
#include "qml/RecordingProjectThumbnail.h"

#include "qml/RecordingRecoveryViewModel.h"
#include "settings/RecordingSettingsManager.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QVariantMap>

namespace SnapTray {

RecordingLibraryViewModel::RecordingLibraryViewModel(QObject* parent)
    : QObject(parent)
{
    connect(RecordingProjects::events(), &RecordingProjectEvents::changed, this, &RecordingLibraryViewModel::refresh);
    connect(RecordingProjects::events(), &RecordingProjectEvents::thumbnailChanged, this, [this](const QString& dir) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i).dir != dir) continue;
            auto row = m_rows.at(i).toMap();
            const auto& project = m_items.at(i);
            const qint64 bytes = project.sizeBytes();
            m_totalBytes += bytes - row.value(QStringLiteral("bytes")).toLongLong();
            row[QStringLiteral("bytes")] = bytes;
            row[QStringLiteral("thumbnail")] = project.thumbnailUrl();
            if (!row.value(QStringLiteral("open")).toBool())
                row[QStringLiteral("details")] = tr("%1, %2, created %3")
                    .arg(RecordingRecoveryViewModel::durationText(project.durationMs),
                         QLocale().formattedDataSize(bytes),
                         QLocale().toString(project.created, QLocale::ShortFormat));
            m_rows[i] = row;
            emit projectsChanged(); // compatibility list property
            emit thumbnailChanged(dir);
            break;
        }
    });
    refresh();
}

void RecordingLibraryViewModel::refresh()
{
    m_items = RecordingProjects::list();
    rebuildRows();
    for (const auto& project : m_items) if (!project.unreadable) requestProjectThumbnail(project);
}

QString RecordingLibraryViewModel::totalSizeText() const
{
    return QLocale().formattedDataSize(m_totalBytes);
}

bool RecordingLibraryViewModel::keepProjects() const
{
    return RecordingSettingsManager::instance().keepProjects();
}

int RecordingLibraryViewModel::cleanupDays() const
{
    return RecordingSettingsManager::instance().projectCleanupDays();
}

void RecordingLibraryViewModel::setOpenProjectDir(const QString& dir)
{
    if (m_openProjectDir == dir) return;
    m_openProjectDir = dir;
    RecordingProjects::setOpenProjectDir(dir);
    rebuildRows();
}

void RecordingLibraryViewModel::rebuildRows()
{
    m_rows.clear();
    m_totalBytes = 0;
    for (const auto& project : m_items) {
        const qint64 bytes = project.sizeBytes();
        m_totalBytes += bytes;
        QVariantMap row;
        row[QStringLiteral("bytes")] = bytes;
        row[QStringLiteral("title")] = project.unreadable ? tr("Unreadable project") : project.title();
        const QString details = tr("%1, %2, created %3")
                                    .arg(RecordingRecoveryViewModel::durationText(project.durationMs),
                                         QLocale().formattedDataSize(bytes),
                                         QLocale().toString(project.created, QLocale::ShortFormat));
        const bool open = !m_openProjectDir.isEmpty() && project.dir == m_openProjectDir;
        row[QStringLiteral("details")] = open ? tr("Open in the preview") : details
            + (project.recoveredFromBackup ? QStringLiteral(" — ") + tr("Recovered from the previous save") : QString());
        row[QStringLiteral("open")] = open;
        row[QStringLiteral("thumbnail")] = project.thumbnailUrl();
        m_rows.append(row);
    }
    emit projectsChanged();
    emit itemsChanged();
}

void RecordingLibraryViewModel::open(int index)
{
    if (index < 0 || index >= m_items.size() || m_items.at(index).dir == m_openProjectDir) return;
    emit openRequested(m_items.at(index).dir);
}

void RecordingLibraryViewModel::remove(int index)
{
    if (index < 0 || index >= m_items.size() || m_items.at(index).dir == m_openProjectDir) return;
    const RecordingProject project = m_items.at(index); // notification may replace m_items
    if (!RecordingProjects::remove(project)) {
        qWarning() << "RecordingLibraryViewModel: could not remove" << project.dir;
        refresh();
    }
}

void RecordingLibraryViewModel::showInFolder(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_items.at(index).dir));
}

void RecordingLibraryViewModel::showSavedFile(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    const QFileInfo saved(m_items.at(index).savedPath);
    if (m_items.at(index).savedPath.isEmpty() || !saved.exists()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(saved.absolutePath()));
}

void RecordingLibraryViewModel::close()
{
    emit closeRequested();
}

} // namespace SnapTray
