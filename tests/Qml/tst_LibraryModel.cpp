#include <QtTest/QtTest>

#include "history/HistoryStore.h"
#include "qml/HistoryModel.h"
#include "qml/LibraryModel.h"
#include "qml/RecordingLibraryViewModel.h"
#include "recording/RecordingProject.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>

using SnapTray::HistoryModel;
using SnapTray::LibraryModel;
using SnapTray::RecordingLibraryViewModel;

namespace {

SnapTray::HistoryEntry writeScreenshot(const QDateTime& createdAt, int seed, qint64 extraBytes = 0)
{
    QImage image(QSize(64, 40), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(seed * 30 % 256, 80, 120));
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = image;
    request.resultImage = image;
    request.selectionRect = QRect(QPoint(0, 0), image.size());
    request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    request.canvasLogicalSize = image.size();
    request.createdAt = createdAt;
    auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    if (!entry) qFatal("history entry");
    if (extraBytes > 0) {
        QFile file(entry->resultPath);
        if (!file.open(QIODevice::Append)) qFatal("append");
        file.write(QByteArray(static_cast<int>(extraBytes), '@'));
    }
    return *entry;
}

// Creates a project and back-dates its manifest so the merge order is known.
QString writeProject(const QTemporaryDir& dir, const QString& name, const QDateTime& created, qint64 bytes,
                     const QString& savedPath)
{
    const QString video = dir.filePath(name);
    QFile file(video);
    if (!file.open(QIODevice::WriteOnly)) qFatal("video");
    file.write(QByteArray(static_cast<int>(bytes), 'v'));
    file.close();
    QImage thumbnail(32, 18, QImage::Format_RGB32);
    thumbnail.fill(Qt::blue);
    const auto project = SnapTray::RecordingProjects::create(video, false, {}, thumbnail, 65000, savedPath);
    if (!project) qFatal("project");
    QFile manifest(project->dir + QStringLiteral("/project.json"));
    if (!manifest.open(QIODevice::ReadOnly)) qFatal("manifest");
    QJsonObject json = QJsonDocument::fromJson(manifest.readAll()).object();
    manifest.close();
    for (const QString& key : json.keys()) {
        if (key.startsWith(QStringLiteral("created")) || key.startsWith(QStringLiteral("modified")))
            json.insert(key, created.toString(Qt::ISODateWithMs));
    }
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate)) qFatal("manifest");
    manifest.write(QJsonDocument(json).toJson());
    return project->dir;
}

} // namespace

class tst_LibraryModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void sourcesFilterKinds();
    void allMergesByCaptureTime();
    void oldestAndLargestOrders();
    void recordingRoles();
    void busyProjectIsMarked();
    void followsSourceChanges();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QDateTime m_now;
    QString m_newProject;
    QString m_oldProject;
    QString m_newShot;
    QString m_oldShot;
};

void tst_LibraryModel::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    qputenv("SNAPTRAY_HISTORY_DIR", m_dir->filePath(QStringLiteral("history")).toLocal8Bit());
    SnapTray::RecordingProjects::setRootForTests(m_dir->filePath(QStringLiteral("projects")));
    m_now = QDateTime::currentDateTime();
    // Newest first: project (now), screenshot (-1 h), project (-2 d), screenshot (-3 d).
    m_newProject = writeProject(*m_dir, QStringLiteral("new.mp4"), m_now, 1000, m_dir->filePath(QStringLiteral("saved-new.mp4")));
    m_newShot = writeScreenshot(m_now.addSecs(-3600), 1).id;
    m_oldProject = writeProject(*m_dir, QStringLiteral("old.mp4"), m_now.addDays(-2), 900000, QString());
    m_oldShot = writeScreenshot(m_now.addDays(-3), 2).id;
}

void tst_LibraryModel::cleanup()
{
    SnapTray::RecordingProjects::setRootForTests(QString());
    qunsetenv("SNAPTRAY_HISTORY_DIR");
    m_dir.reset();
}

void tst_LibraryModel::sourcesFilterKinds()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    QCOMPARE(model.screenshotCount(), 2);
    QCOMPARE(model.recordingCount(), 2);
    QCOMPARE(model.allCount(), 4);

    model.setSource(LibraryModel::Screenshots);
    QCOMPARE(model.rowCount(), 2);
    for (int i = 0; i < 2; ++i) QCOMPARE(model.kindAt(i), int(LibraryModel::Screenshot));

    model.setSource(LibraryModel::Recordings);
    QCOMPARE(model.rowCount(), 2);
    for (int i = 0; i < 2; ++i) QCOMPARE(model.kindAt(i), int(LibraryModel::Recording));

    model.setSource(LibraryModel::AllCaptures);
    QCOMPARE(model.rowCount(), 4);
}

void tst_LibraryModel::allMergesByCaptureTime()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    QCOMPARE(model.source(), int(LibraryModel::AllCaptures));
    QCOMPARE(model.keyAt(0), m_newProject);
    QCOMPARE(model.keyAt(1), m_newShot);
    QCOMPARE(model.keyAt(2), m_oldProject);
    QCOMPARE(model.keyAt(3), m_oldShot);
    // Each row points back into its own source.
    QCOMPARE(history.idAt(model.sourceRowAt(1)), m_newShot);
    QCOMPARE(recordings.items().at(model.sourceRowAt(2)).dir, m_oldProject);
    QCOMPARE(model.indexOfKey(m_oldProject), 2);
    QCOMPARE(model.indexOfKey(QStringLiteral("missing")), -1);
}

void tst_LibraryModel::oldestAndLargestOrders()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    history.setSortOrder(HistoryModel::OldestFirst);
    QCOMPARE(model.keyAt(0), m_oldShot);
    QCOMPARE(model.keyAt(3), m_newProject);
    history.setSortOrder(HistoryModel::LargestFirst);
    QCOMPARE(model.keyAt(0), m_oldProject); // 900 KB recording
    model.setSource(LibraryModel::Recordings);
    QCOMPARE(model.keyAt(0), m_oldProject);
    history.setSortOrder(HistoryModel::NewestFirst);
    QCOMPARE(model.keyAt(0), m_newProject);
}

void tst_LibraryModel::recordingRoles()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    const QModelIndex saved = model.index(model.indexOfKey(m_newProject));
    QCOMPARE(model.data(saved, LibraryModel::KindLabelRole).toString(), QStringLiteral("Editable project"));
    QCOMPARE(model.data(saved, LibraryModel::TitleRole).toString(), QStringLiteral("saved-new.mp4"));
    QVERIFY(model.data(saved, LibraryModel::ThumbnailUrlRole).toString().startsWith(QStringLiteral("file:")));
    QVERIFY(model.data(saved, LibraryModel::MetaTextRole).toString().contains(QStringLiteral("1:05")));
    // The saved file does not exist on disk.
    QCOMPARE(model.data(saved, LibraryModel::DetailTextRole).toString(),
             QStringLiteral("Saved file was moved or deleted"));
    QCOMPARE(model.data(saved, LibraryModel::SavedFileExistsRole).toBool(), false);
    QFile file(m_dir->filePath(QStringLiteral("saved-new.mp4")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    recordings.refresh();
    const QModelIndex refreshed = model.index(model.indexOfKey(m_newProject));
    QCOMPARE(model.data(refreshed, LibraryModel::DetailTextRole).toString(), QStringLiteral("Saved as saved-new.mp4"));
    QCOMPARE(model.data(refreshed, LibraryModel::SavedFileExistsRole).toBool(), true);

    const QModelIndex unsaved = model.index(model.indexOfKey(m_oldProject));
    QCOMPARE(model.data(unsaved, LibraryModel::DetailTextRole).toString(), QStringLiteral("Not saved yet"));

    const QModelIndex shot = model.index(model.indexOfKey(m_newShot));
    QCOMPARE(model.data(shot, LibraryModel::KindLabelRole).toString(), QStringLiteral("Screenshot"));
    QCOMPARE(model.data(shot, LibraryModel::BusyRole).toBool(), false);
}

void tst_LibraryModel::busyProjectIsMarked()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    recordings.setOpenProjectDir(m_oldProject);
    const QModelIndex busy = model.index(model.indexOfKey(m_oldProject));
    QCOMPARE(model.data(busy, LibraryModel::BusyRole).toBool(), true);
    QCOMPARE(model.data(busy, LibraryModel::DetailTextRole).toString(), QStringLiteral("Open in the recording preview"));
}

void tst_LibraryModel::followsSourceChanges()
{
    HistoryModel history;
    RecordingLibraryViewModel recordings;
    LibraryModel model(&history, &recordings);
    QSignalSpy counts(&model, &LibraryModel::countsChanged);
    int oldRow = -1;
    for (int i = 0; i < recordings.items().size(); ++i)
        if (recordings.items().at(i).dir == m_oldProject) oldRow = i;
    QVERIFY(oldRow >= 0);
    recordings.remove(oldRow);
    QCOMPARE(model.recordingCount(), 1);
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(counts.count() > 0);
    writeScreenshot(m_now, 5);
    history.refresh();
    QCOMPARE(model.screenshotCount(), 3);
    QCOMPARE(model.rowCount(), 4);
    // A smart folder narrows the screenshots shown, not the totals.
    history.setActiveFilter(HistoryModel::LargeFiles);
    QCOMPARE(model.screenshotCount(), 3);
}

QTEST_MAIN(tst_LibraryModel)
#include "tst_LibraryModel.moc"
