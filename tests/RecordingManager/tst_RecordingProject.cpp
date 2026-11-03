#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "recording/AudioTracks.h"
#include "recording/CursorTimeline.h"
#include "recording/RecordingProject.h"

using namespace SnapTray;

// Editable recording projects (#191): the versioned recipe and the project
// folder lifecycle.
class tst_RecordingProject : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void recipeRoundTripsEveryEdit();
    void recipeMigratesVersionZero();
    void recipeRejectsWhatIsNotARecipe();
    void recipeToleratesUnknownKeys();
    void createMovesTheRecordingAndSidecars();
    void createCanCopy();
    void createFailsCleanly();
    void failedCreateNeverLosesTheSource();
    void loadRejectsDamagedProjects();
    void updateKeepsTheSourceAndBumpsModified();
    void listIsNewestFirst();
    void removeOnlyInsideTheRoot();
    void removeOlderThan();

private:
    static PreviewEditState sampleState();
    QString writeRecording(const QString& name, bool withSidecars);

    QTemporaryDir m_dir;
    QString m_root;
};

void tst_RecordingProject::init()
{
    QVERIFY(m_dir.isValid());
    m_root = QDir(m_dir.path()).filePath(QStringLiteral("projects-%1").arg(QDateTime::currentMSecsSinceEpoch()));
    RecordingProjects::setRootForTests(m_root);
}

void tst_RecordingProject::cleanup()
{
    QDir(m_root).removeRecursively();
    RecordingProjects::setRootForTests(QString());
}

PreviewEditState tst_RecordingProject::sampleState()
{
    PreviewEditState s;
    s.crop = QRect(10, 20, 300, 200);
    s.trimStart = 500;
    s.trimEnd = 4500;
    s.cursor.scale = 1.5;
    s.cursor.clickRipple = true;
    s.cursor.idleHideMs = 2000;
    s.timeline.splitAt(1500, 5000);
    s.timeline.setSpeed(1, 2.0);
    s.keys.position = KeyOverlaySettings::Position::TopCenter;
    s.keys.displayMs = 2500;
    OverlaySegment text;
    text.text = QStringLiteral("Step 1");
    text.startMs = 600;
    text.endMs = 1600;
    s.overlays.segments.push_back(text);
    s.audio.microphoneGainDb = 6.0;
    s.audio.systemAudioMuted = true;
    s.audio.microphoneNoiseReduction = true;
    return s;
}

QString tst_RecordingProject::writeRecording(const QString& name, bool withSidecars)
{
    const QString path = QDir(m_dir.path()).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QByteArray(1024, 'v'));
    file.close();
    if (withSidecars) {
        for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
            QFile sidecar(path + suffix);
            if (!sidecar.open(QIODevice::WriteOnly)) return {};
            sidecar.write(suffix.toUtf8());
        }
    }
    return path;
}

void tst_RecordingProject::recipeRoundTripsEveryEdit()
{
    const PreviewEditState state = sampleState();
    const QJsonObject choices{{QStringLiteral("format"), 0}};
    const QJsonObject recipe = RecordingRecipe::toJson(state, choices);
    QCOMPARE(recipe.value(QStringLiteral("version")).toInt(), RecordingRecipe::kVersion);
    // Through text, as on disk.
    const QJsonObject reread = QJsonDocument::fromJson(QJsonDocument(recipe).toJson()).object();
    const auto restored = RecordingRecipe::editState(reread);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->crop, state.crop);
    QCOMPARE(restored->trimStart, state.trimStart);
    QCOMPARE(restored->trimEnd, state.trimEnd);
    QVERIFY(restored->cursor == state.cursor);
    QVERIFY(restored->timeline == state.timeline);
    QVERIFY(restored->keys == state.keys);
    QVERIFY(restored->overlays == state.overlays);
    QVERIFY(restored->audio == state.audio);
    QVERIFY(*restored == state);
    QCOMPARE(RecordingRecipe::exportChoices(reread), choices);
}

void tst_RecordingProject::recipeMigratesVersionZero()
{
    // Development builds wrote the edit fields at the top level.
    QJsonObject flat = RecordingRecipe::toJson(sampleState(), {}).value(QStringLiteral("edit")).toObject();
    flat.insert(QStringLiteral("export"), QJsonObject{{QStringLiteral("format"), 1}});
    const QJsonObject migrated = RecordingRecipe::migrate(flat);
    QCOMPARE(migrated.value(QStringLiteral("version")).toInt(), RecordingRecipe::kVersion);
    const auto restored = RecordingRecipe::editState(flat);
    QVERIFY(restored.has_value());
    QVERIFY(*restored == sampleState());
    QCOMPARE(RecordingRecipe::exportChoices(flat).value(QStringLiteral("format")).toInt(), 1);
}

void tst_RecordingProject::recipeRejectsWhatIsNotARecipe()
{
    QVERIFY(!RecordingRecipe::editState({}).has_value());
    QVERIFY(!RecordingRecipe::editState({{QStringLiteral("type"), QStringLiteral("something-else")}}).has_value());
}

void tst_RecordingProject::recipeToleratesUnknownKeys()
{
    // A newer version is read for what it shares with this one.
    QJsonObject recipe = RecordingRecipe::toJson(sampleState(), {});
    recipe.insert(QStringLiteral("version"), RecordingRecipe::kVersion + 1);
    QJsonObject edit = recipe.value(QStringLiteral("edit")).toObject();
    edit.insert(QStringLiteral("futureEffect"), QJsonObject{{QStringLiteral("x"), 1}});
    recipe.insert(QStringLiteral("edit"), edit);
    const auto restored = RecordingRecipe::editState(recipe);
    QVERIFY(restored.has_value());
    QVERIFY(*restored == sampleState());
    // A damaged trim never becomes an empty range.
    edit.insert(QStringLiteral("trimEnd"), 100.0);
    edit.insert(QStringLiteral("trimStart"), 200.0);
    recipe.insert(QStringLiteral("edit"), edit);
    QCOMPARE(RecordingRecipe::editState(recipe)->trimEnd, qint64(-1));
}

void tst_RecordingProject::createMovesTheRecordingAndSidecars()
{
    const QString source = writeRecording(QStringLiteral("SnapTray_Recording_a.mp4"), true);
    QVERIFY(!source.isEmpty());
    QImage thumbnail(32, 18, QImage::Format_RGB32);
    thumbnail.fill(Qt::red);
    const QJsonObject recipe = RecordingRecipe::toJson(sampleState(), {});
    const auto project = RecordingProjects::create(source, false, recipe, thumbnail, 5000, QStringLiteral("/saved/a.mp4"));
    QVERIFY(project.has_value());
    QVERIFY(RecordingProjects::contains(project->dir));
    QVERIFY(RecordingProjects::contains(project->videoPath()));
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(project->videoPath()));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QVERIFY2(!QFileInfo::exists(source + suffix), qPrintable(suffix));
        QVERIFY2(QFileInfo::exists(project->videoPath() + suffix), qPrintable(suffix));
    }
    QVERIFY(QFileInfo::exists(project->thumbnailPath()));

    const auto loaded = RecordingProjects::load(project->dir);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->videoPath(), project->videoPath());
    QCOMPARE(loaded->durationMs, qint64(5000));
    QCOMPARE(loaded->savedPath, QStringLiteral("/saved/a.mp4"));
    QCOMPARE(loaded->title(), QStringLiteral("a.mp4"));
    QVERIFY(*RecordingRecipe::editState(loaded->recipe) == sampleState());
    QVERIFY(loaded->sizeBytes() >= 1024);
    QCOMPARE(RecordingProjects::totalBytes(), loaded->sizeBytes());
}

void tst_RecordingProject::createCanCopy()
{
    const QString source = writeRecording(QStringLiteral("SnapTray_Recording_b.mp4"), true);
    const auto project = RecordingProjects::create(source, true, {}, {}, 1000, {});
    QVERIFY(project.has_value());
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(source + QLatin1String(CursorTimelineSidecar::kSuffix)));
    QVERIFY(QFileInfo::exists(project->videoPath() + QLatin1String(CursorTimelineSidecar::kSuffix)));
    // No saved file yet: the date names it.
    QVERIFY(!project->title().isEmpty());
}

void tst_RecordingProject::createFailsCleanly()
{
    QVERIFY(!RecordingProjects::create(QDir(m_dir.path()).filePath(QStringLiteral("missing.mp4")), false, {}, {}, 0, {})
                 .has_value());
    QVERIFY(RecordingProjects::list().isEmpty());
}

// PR #203 review: a failure after the recording was moved (a full disk at
// the manifest) moves everything back instead of deleting the only copy.
void tst_RecordingProject::failedCreateNeverLosesTheSource()
{
    const QString source = writeRecording(QStringLiteral("SnapTray_Recording_keep.mp4"), true);
    QVERIFY(!source.isEmpty());
    RecordingProjects::failNextManifestWriteForTests();
    QVERIFY(!RecordingProjects::create(source, false, {}, {}, 1000, {}).has_value());
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(QFileInfo(source).size(), qint64(1024));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QVERIFY2(QFileInfo::exists(source + suffix), qPrintable(suffix));
    }
    QVERIFY(QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(RecordingProjects::list().isEmpty());
    // The next try works.
    QVERIFY(RecordingProjects::create(source, false, {}, {}, 1000, {}).has_value());
}

void tst_RecordingProject::loadRejectsDamagedProjects()
{
    const auto project = RecordingProjects::create(writeRecording(QStringLiteral("c.mp4"), false), false, {}, {}, 0, {});
    QVERIFY(project.has_value());
    const QString manifest = QDir(project->dir).filePath(QStringLiteral("project.json"));

    // The video name never leads out of the project folder.
    QFile::remove(manifest);
    QFile file(manifest);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("snaptray-project")},
                                         {QStringLiteral("version"), 1},
                                         {QStringLiteral("video"), QStringLiteral("../../c.mp4")}})
                   .toJson());
    file.close();
    QVERIFY(!RecordingProjects::load(project->dir).has_value());

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not json");
    file.close();
    QVERIFY(!RecordingProjects::load(project->dir).has_value());
    QVERIFY(RecordingProjects::list().isEmpty());
}

void tst_RecordingProject::updateKeepsTheSourceAndBumpsModified()
{
    auto project = RecordingProjects::create(writeRecording(QStringLiteral("d.mp4"), false), false, {}, {}, 0,
                                             QStringLiteral("/saved/first.mp4"));
    QVERIFY(project.has_value());
    const QDateTime before = project->modified;
    QTest::qWait(5);
    const QJsonObject recipe = RecordingRecipe::toJson(sampleState(), {});
    QVERIFY(RecordingProjects::update(*project, recipe, QStringLiteral("/saved/second.mp4")));
    const auto loaded = RecordingProjects::load(project->dir);
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->modified > before);
    QCOMPARE(loaded->created, project->created);
    QCOMPARE(loaded->savedPath, QStringLiteral("/saved/second.mp4"));
    QVERIFY(*RecordingRecipe::editState(loaded->recipe) == sampleState());
    QVERIFY(QFileInfo::exists(loaded->videoPath()));
}

void tst_RecordingProject::listIsNewestFirst()
{
    auto older = RecordingProjects::create(writeRecording(QStringLiteral("e.mp4"), false), false, {}, {}, 0, {});
    QTest::qWait(5);
    auto newer = RecordingProjects::create(writeRecording(QStringLiteral("f.mp4"), false), false, {}, {}, 0, {});
    QVERIFY(older && newer);
    QList<RecordingProject> projects = RecordingProjects::list();
    QCOMPARE(projects.size(), 2);
    QCOMPARE(projects.first().dir, newer->dir);
    // Saving the older one again brings it to the top.
    QTest::qWait(5);
    QVERIFY(RecordingProjects::update(*older, {}, {}));
    projects = RecordingProjects::list();
    QCOMPARE(projects.first().dir, older->dir);
}

void tst_RecordingProject::removeOnlyInsideTheRoot()
{
    const auto project = RecordingProjects::create(writeRecording(QStringLiteral("g.mp4"), false), false, {}, {}, 0, {});
    QVERIFY(project.has_value());

    RecordingProject outside = *project;
    outside.dir = m_dir.path();
    QVERIFY(!RecordingProjects::remove(outside));
    RecordingProject root = *project;
    root.dir = m_root;
    QVERIFY(!RecordingProjects::remove(root));
    QVERIFY(!RecordingProjects::contains(m_root + QStringLiteral("-sibling/x.mp4")));
    QVERIFY(QDir(m_dir.path()).exists());

    QVERIFY(RecordingProjects::remove(*project));
    QVERIFY(!QFileInfo::exists(project->dir));
    QVERIFY(QDir(m_root).exists());
}

void tst_RecordingProject::removeOlderThan()
{
    auto project = RecordingProjects::create(writeRecording(QStringLiteral("h.mp4"), false), false, {}, {}, 0, {});
    QVERIFY(project.has_value());
    const QDateTime now = QDateTime::currentDateTime();
    QCOMPARE(RecordingProjects::removeOlderThan(0, now.addDays(365)), 0); // never
    QCOMPARE(RecordingProjects::removeOlderThan(30, now.addDays(29)), 0);
    QVERIFY(QFileInfo::exists(project->dir));
    QCOMPARE(RecordingProjects::removeOlderThan(30, now.addDays(31)), 1);
    QVERIFY(!QFileInfo::exists(project->dir));
}

QTEST_GUILESS_MAIN(tst_RecordingProject)
#include "tst_RecordingProject.moc"
