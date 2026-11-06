#include "qml/RecordingLibraryViewModel.h"

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
    refresh();
}

void RecordingLibraryViewModel::refresh()
{
    m_items = RecordingProjects::list();
    rebuildRows();
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
        row[QStringLiteral("title")] = project.title();
        const QString details = tr("%1, %2, created %3")
                                    .arg(RecordingRecoveryViewModel::durationText(project.durationMs),
                                         QLocale().formattedDataSize(bytes),
                                         QLocale().toString(project.created, QLocale::ShortFormat));
        const bool open = !m_openProjectDir.isEmpty() && project.dir == m_openProjectDir;
        row[QStringLiteral("details")] = open ? tr("Open in the preview") : details;
        row[QStringLiteral("open")] = open;
        row[QStringLiteral("thumbnail")] = QFileInfo::exists(project.thumbnailPath())
            ? QUrl::fromLocalFile(project.thumbnailPath()).toString()
            : QString();
        m_rows.append(row);
    }
    emit projectsChanged();
}

void RecordingLibraryViewModel::open(int index)
{
    if (index < 0 || index >= m_items.size() || m_items.at(index).dir == m_openProjectDir) return;
    emit openRequested(m_items.at(index).dir);
}

void RecordingLibraryViewModel::remove(int index)
{
    if (index < 0 || index >= m_items.size() || m_items.at(index).dir == m_openProjectDir) return;
    if (!RecordingProjects::remove(m_items.at(index))) {
        qWarning() << "RecordingLibraryViewModel: could not remove" << m_items.at(index).dir;
    }
    refresh();
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
