#pragma once

#include "recording/RecordingProject.h"

#include <QObject>
#include <QVariantList>

namespace SnapTray {

// The kept recording projects (#191), for the Recording Library dialog.
class RecordingLibraryViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QString totalSizeText READ totalSizeText NOTIFY projectsChanged)
    Q_PROPERTY(bool keepProjects READ keepProjects CONSTANT)

public:
    explicit RecordingLibraryViewModel(QObject* parent = nullptr);

    // Re-reads the projects folder.
    Q_INVOKABLE void refresh();
    QVariantList projects() const { return m_rows; }
    QString totalSizeText() const;
    bool keepProjects() const;
    int count() const { return m_items.size(); }
    const QList<RecordingProject>& items() const { return m_items; }
    QString openProjectDir() const { return m_openProjectDir; }
    // The project open in the preview: it can be neither opened again nor
    // deleted until the preview closes.
    void setOpenProjectDir(const QString& dir);

    // Opens the project in the preview (the dialog closes on success).
    Q_INVOKABLE void open(int index);
    // Deletes the project folder: its recording, sidecars and edits. The
    // file saved from it stays.
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void showInFolder(int index);
    Q_INVOKABLE void close();

signals:
    void projectsChanged();
    void openRequested(const QString& projectDir);
    void closeRequested();

private:
    void rebuildRows();

    QList<RecordingProject> m_items;
    QVariantList m_rows;
    qint64 m_totalBytes = 0;
    QString m_openProjectDir;
};

} // namespace SnapTray
