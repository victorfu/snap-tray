#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <filesystem>

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
    void createMovesTracksThePreviewIsPlaying();
    void createCanCopy();
    void stagingKeepsSourceUntilCommit_data();
    void stagingKeepsSourceUntilCommit();
    void createFailsCleanly();
    void failedCleanupIsRetriedSafely();
    void preparerReceivesRecipe();
    void preparedRecipeInvalidatesSuppliedThumbnail();
    void failedCreateNeverLosesTheSource();
    void failedTransferPreservesEverySource_data();
    void failedTransferPreservesEverySource();
    void loadRejectsDamagedProjects();
    void updateKeepsTheSourceAndBumpsModified();
    void listIsNewestFirst();
    void removeOnlyInsideTheRoot();
    void removeOlderThan();
    void thumbnailPublicationFollowsCommittedGeneration();
    void legacyThumbnailIsNeverTrusted();
    void unchangedEditsKeepThumbnail();
    void thumbnailUrlUsesLoadedSnapshot();

private:
    static PreviewEditState sampleState();
    QString writeRecording(const QString& name, bool withSidecars);

    QTemporaryDir m_dir;
    QString m_root;
};

void tst_RecordingProject::preparedRecipeInvalidatesSuppliedThumbnail()
{
    const QString source = writeRecording(QStringLiteral("prepared-poster.mp4"), false);
    const auto original = RecordingRecipe::toJson(PreviewEditState{}, {});
    PreviewEditState edited;
    edited.redaction.masks.push_back({QRect(0, 0, 32, 18), 0, 5000});
    const auto prepared = RecordingRecipe::toJson(edited, {});
    QImage oldPoster(32, 18, QImage::Format_RGB32);
    oldPoster.fill(Qt::green);
    QSignalSpy changes(RecordingProjects::events(), &RecordingProjectEvents::changed);
    QString pendingDir;
    RecordingProjects::beforeNextManifestWriteForTests([&] {
        QVERIFY(QFile::exists(source));
        QVERIFY(RecordingProjects::list().isEmpty());
        QCOMPARE(changes.count(), 0);
        QVERIFY(QDir(pendingDir).entryList({QStringLiteral("thumbnail*.png")}, QDir::Files).isEmpty());
    });
    const auto project = RecordingProjects::create(source, false, original, oldPoster, 5000, {},
        [&](const QString& dir, const QJsonObject&) -> std::optional<QJsonObject> {
            pendingDir = dir;
            return prepared;
        });
    QVERIFY(project);
    QCOMPARE(project->recipe, prepared);
    QCOMPARE(changes.count(), 1);
    QVERIFY(!QFile::exists(source));
    // The supplied unmasked poster belongs to the pre-preparation recipe.
    // Only a newly rendered image for the committed recipe may be published.
    QVERIFY(project->thumbnailUrl().isEmpty());
    QVERIFY(!QFile::exists(project->thumbnailPath()));
    oldPoster.fill(Qt::black);
    QVERIFY(RecordingProjects::publishThumbnail(*project, oldPoster));
    QVERIFY(!project->thumbnailUrl().isEmpty());
}

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

// The preview plays the separate tracks (#189) while the save keeps the
// project: the tracks still move, and what the preview reads stays readable.
// On Windows a track open without share-delete could not be moved or
// removed, so every kept project left a copy of both in %TEMP% (#210).
void tst_RecordingProject::createMovesTracksThePreviewIsPlaying()
{
    const QString source = writeRecording(QStringLiteral("SnapTray_Recording_playing.mp4"), false);
    QVERIFY(!source.isEmpty());
    QByteArray pcm(4800 * AudioTracks::kBytesPerFrame, '\0');
    for (qsizetype i = 0; i < pcm.size(); i += 2) qToLittleEndian<qint16>(qint16(i % 2000), pcm.data() + i);
    for (const auto track : {AudioTracks::Track::Microphone, AudioTracks::Track::SystemAudio}) {
        AudioTrackWriter writer(AudioTracks::pathFor(source, track));
        QVERIFY(writer.open());
        writer.write(pcm, 0);
    }
    AudioTrackMixer playing(source, AudioMixSettings{});
    QVERIFY(playing.isValid());
    const QByteArray before = playing.read(100, 960);

    const auto project = RecordingProjects::create(source, false, {}, {}, 100, {});
    QVERIFY(project.has_value());
    for (const auto track : {AudioTracks::Track::Microphone, AudioTracks::Track::SystemAudio}) {
        QVERIFY2(!QFileInfo::exists(AudioTracks::pathFor(source, track)), qPrintable(AudioTracks::pathFor(source, track)));
        QVERIFY(QFileInfo::exists(AudioTracks::pathFor(project->videoPath(), track)));
    }
    QCOMPARE(playing.read(100, 960), before);

    // A plain save (no project) removes the tracks while they still play.
    AudioTrackMixer stillPlaying(project->videoPath(), AudioMixSettings{});
    QVERIFY(stillPlaying.isValid());
    AudioTracks::remove(project->videoPath());
    QVERIFY(!AudioTracks::exist(project->videoPath()));
    QVERIFY(!QFileInfo::exists(AudioTracks::pathFor(project->videoPath(), AudioTracks::Track::Microphone)));
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

void tst_RecordingProject::stagingKeepsSourceUntilCommit_data()
{
    QTest::addColumn<bool>("copy");
    QTest::addColumn<bool>("symlinkSource");
    QTest::newRow("move-uses-links") << false << false;
    QTest::newRow("copy-is-independent") << true << false;
#ifdef Q_OS_UNIX
    QTest::newRow("symlink-source-copies-target") << false << true;
#endif
}

void tst_RecordingProject::stagingKeepsSourceUntilCommit()
{
    QFETCH(bool, copy);
    QFETCH(bool, symlinkSource);
    QString source = writeRecording(QStringLiteral("staging.mp4"), true);
    if (symlinkSource) {
        const QString alias = QDir(m_dir.path()).filePath("staging-alias.mp4");
        QVERIFY(QFile::link(source, alias));
        for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
            QVERIFY(QFile::link(source + suffix, alias + suffix));
        }
        source = alias;
    }
    const auto native = [](const QString& path) { return std::filesystem::u8path(path.toUtf8().constData()); };
    const QString probe = source + QStringLiteral(".link-probe");
    std::error_code error;
    std::filesystem::create_hard_link(native(source), native(probe), error);
    if (error) QSKIP("Fixture filesystem does not support hard links");
    QVERIFY(QFile::remove(probe));
    bool inspected = false;
    RecordingProjects::beforeNextManifestWriteForTests([&] {
        inspected = true;
        const auto dirs = QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(dirs.size(), 1);
        const QString staged = QDir(m_root).filePath(dirs.first() + "/recording.mp4");
        QStringList suffixes = RecordingProjects::sidecarSuffixes();
        suffixes.prepend(QString());
        for (const QString& suffix : suffixes) {
            QVERIFY(QFile::exists(source + suffix));
            const bool linked = std::filesystem::equivalent(native(source + suffix), native(staged + suffix), error);
            QVERIFY(!error);
            QCOMPARE(linked, !copy && !symlinkSource);
        }
    });
    RecordingProjects::failNextManifestWriteForTests();
    QVERIFY(!RecordingProjects::create(source, copy, {}, {}, 1000, {}));
    QVERIFY(inspected);
    QCOMPARE(QFileInfo(source).size(), qint64(1024));
    QVERIFY(QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

void tst_RecordingProject::createFailsCleanly()
{
    QVERIFY(!RecordingProjects::create(QDir(m_dir.path()).filePath(QStringLiteral("missing.mp4")), false, {}, {}, 0, {})
                 .has_value());
    QVERIFY(RecordingProjects::list().isEmpty());
}

// A manifest failure leaves the source and every sidecar in place for retry.
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

void tst_RecordingProject::failedTransferPreservesEverySource_data()
{
    QTest::addColumn<QString>("blockedSuffix");
    QTest::newRow("video") << QString();
    QTest::newRow("last-sidecar") << RecordingProjects::sidecarSuffixes().last();
}

void tst_RecordingProject::failedTransferPreservesEverySource()
{
    QFETCH(QString, blockedSuffix);
    const QString source = writeRecording(QStringLiteral("transfer.mp4"), true);
    QVERIFY(!source.isEmpty());
    const auto prepare = [&](const QString& dir, const QJsonObject&) -> std::optional<QJsonObject> {
        // Occupy the copy destination to fail after asset preparation, and
        // (for the last sidecar) after the other media files were staged.
        if (!QDir(dir).mkdir(QStringLiteral("recording.mp4") + blockedSuffix)) return std::nullopt;
        return QJsonObject{};
    };
    QVERIFY(!RecordingProjects::create(source, false, {}, {}, 1000, {}, prepare));
    QCOMPARE(QFileInfo(source).size(), qint64(1024));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QFile file(source + suffix);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), suffix.toUtf8());
    }
    QVERIFY(QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(RecordingProjects::create(source, false, {}, {}, 1000, {}));
}

void tst_RecordingProject::failedCleanupIsRetriedSafely()
{
    QVERIFY(QDir().mkpath(QDir(m_root).filePath("legacy-unreadable")));
    const QString source = writeRecording(QStringLiteral("cleanup.mp4"), true);
    RecordingProjects::failNextManifestWriteForTests();
    RecordingProjects::failNextProjectCleanupForTests();
    QVERIFY(!RecordingProjects::create(source, false, {}, {}, 0, {}));
    QVERIFY(QFileInfo::exists(source));
    QDir root(m_root);
    auto markers = root.entryList({".creating-*.pending"}, QDir::Files | QDir::Hidden);
    QCOMPARE(markers.size(), 1);
    const QString pending = markers.first();
    auto dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    dirs.removeAll("legacy-unreadable");
    QCOMPARE(dirs.size(), 1);
    const QString incomplete = root.filePath(dirs.first());
    QVERIFY(QFileInfo::exists(QDir(incomplete).filePath("recording.mp4")));
    // A live creator's lock protects its staged directory from another save.
    QLockFile active(root.filePath(pending + ".lock"));
    QVERIFY(active.tryLock(0));
    auto committed = RecordingProjects::create(writeRecording("second.mp4", false), false, {}, {}, 0, {});
    QVERIFY(committed);
    QVERIFY(QFileInfo::exists(incomplete));
    active.unlock();
    // Simulate a crash after commit but before the marker was removed.
    const QString committedMarker = root.filePath(".creating-" + QFileInfo(committed->dir).fileName() + ".pending");
    QFile marker(committedMarker);
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();
    QVERIFY(RecordingProjects::create(source, false, {}, {}, 0, {}));
    QVERIFY(!QFileInfo::exists(incomplete));
    QVERIFY(RecordingProjects::load(committed->dir));
    QVERIFY(QFileInfo::exists(root.filePath("legacy-unreadable")));
    QVERIFY(root.entryList({".creating-*.pending"}, QDir::Files | QDir::Hidden).isEmpty());
}

void tst_RecordingProject::preparerReceivesRecipe()
{
    const QJsonObject original{{"snapshot", 42}};
    QJsonObject received;
    auto project = RecordingProjects::create(writeRecording("snapshot.mp4", false), false, original, {}, 0, {},
        [&](const QString&, const QJsonObject& recipe) -> std::optional<QJsonObject> {
            received = recipe;
            auto prepared = recipe;
            prepared.insert("prepared", true);
            return prepared;
        });
    QVERIFY(project);
    QCOMPARE(received, original);
    QCOMPARE(project->recipe["snapshot"].toInt(), 42);
    QVERIFY(project->recipe["prepared"].toBool());
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

void tst_RecordingProject::thumbnailPublicationFollowsCommittedGeneration()
{
    auto project = RecordingProjects::create(writeRecording(QStringLiteral("poster.mp4"), false),
                                              false, RecordingRecipe::toJson(sampleState(), {}), {}, 5000, {});
    QVERIFY(project);
    QImage image(32, 18, QImage::Format_RGB32);
    image.fill(Qt::black);
    const RecordingProject oldJob = *project;
    QVERIFY(project->thumbnailUrl().isEmpty());
    QVERIFY(RecordingProjects::publishThumbnail(*project, image));
    const QString oldUrl = project->thumbnailUrl();
    QVERIFY(!oldUrl.isEmpty());

    // A failed manifest write keeps both the in-memory and committed recipe,
    // generation and valid poster. It must not invalidate a successful save.
    const auto recipe = RecordingRecipe::toJson(PreviewEditState{}, {});
    RecordingProjects::failNextManifestWriteForTests();
    QVERIFY(!RecordingProjects::update(*project, recipe, QStringLiteral("failed.mp4")));
    QCOMPARE(project->recipe, oldJob.recipe);
    QCOMPARE(project->thumbnailUrl(), oldUrl);

    QVERIFY(RecordingProjects::update(*project, recipe, {}));
    QVERIFY(project->thumbnailPath() != oldJob.thumbnailPath());
    QVERIFY(project->thumbnailUrl().isEmpty());
    QVERIFY(oldJob.thumbnailUrl().isEmpty()); // cached model snapshot is stale too
    QVERIFY(!RecordingProjects::publishThumbnail(oldJob, image)); // old worker completes late
    QVERIFY(!QFileInfo::exists(oldJob.thumbnailPath()));
    QVERIFY(!RecordingProjects::publishThumbnail(*project, {})); // failed rendering
    QVERIFY(project->thumbnailUrl().isEmpty());

    // Failure to publish stays a placeholder, never an old/raw fallback.
    QVERIFY(QDir().mkdir(project->thumbnailPath()));
    QVERIFY(!RecordingProjects::publishThumbnail(*project, image));
    QVERIFY(QDir().rmdir(project->thumbnailPath()));
    QVERIFY(RecordingProjects::publishThumbnail(*project, image));
    QVERIFY(project->thumbnailUrl() != oldUrl);
    const auto savedJob = *project;
    QVERIFY(RecordingProjects::remove(*project));
    QVERIFY(!RecordingProjects::publishThumbnail(savedJob, image));
    QVERIFY(!QFileInfo::exists(project->dir));
}

void tst_RecordingProject::legacyThumbnailIsNeverTrusted()
{
    const auto project = RecordingProjects::create(writeRecording(QStringLiteral("legacy.mp4"), false),
        false, RecordingRecipe::toJson(sampleState(), {}), {}, 5000, {});
    QVERIFY(project);
    const QString manifest = QDir(project->dir).filePath(QStringLiteral("project.json"));
    QFile file(manifest);
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto json = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    json.remove(QStringLiteral("thumbnailGeneration"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(json).toJson());
    file.close();
    QImage raw(32, 18, QImage::Format_RGB32);
    raw.fill(Qt::green);
    const QString legacy = QDir(project->dir).filePath(QStringLiteral("thumbnail.png"));
    QVERIFY(raw.save(legacy));
    const auto loaded = RecordingProjects::load(project->dir);
    QVERIFY(loaded);
    QVERIFY(loaded->thumbnailGeneration.isEmpty());
    QVERIFY(loaded->thumbnailPath() != legacy);
    QVERIFY(loaded->thumbnailUrl().isEmpty());
    raw.fill(Qt::black);
    QVERIFY(RecordingProjects::publishThumbnail(*loaded, raw));
    QVERIFY(!loaded->thumbnailUrl().isEmpty());
    QCOMPARE(QImage(loaded->thumbnailPath()).pixelColor(0, 0), QColor(Qt::black));
}

void tst_RecordingProject::unchangedEditsKeepThumbnail()
{
    QImage image(32, 18, QImage::Format_RGB32);
    image.fill(Qt::black);
    auto recipe = RecordingRecipe::toJson(sampleState(), {});
    auto project = RecordingProjects::create(writeRecording(QStringLiteral("unchanged.mp4"), false),
                                              false, recipe, image, 5000, {});
    QVERIFY(project);
    const QString path = project->thumbnailPath();
    QVERIFY(RecordingProjects::update(*project, recipe, QStringLiteral("another.mp4")));
    QCOMPARE(project->thumbnailPath(), path);
    QVERIFY(!project->thumbnailUrl().isEmpty());
    recipe.insert(QStringLiteral("export"), QJsonObject{{QStringLiteral("format"), 2}});
    QVERIFY(RecordingProjects::update(*project, recipe, {}));
    QCOMPARE(project->thumbnailPath(), path);
    QVERIFY(!project->thumbnailUrl().isEmpty());
}

void tst_RecordingProject::thumbnailUrlUsesLoadedSnapshot()
{
    QImage image(32, 18, QImage::Format_RGB32);
    image.fill(Qt::black);
    auto project = RecordingProjects::create(writeRecording(QStringLiteral("cached.mp4"), false),
        false, RecordingRecipe::toJson(sampleState(), {}), image, 5000, {});
    QVERIFY(project);
    const QString expected = project->thumbnailUrl();
    QVERIFY(!expected.isEmpty());
    const QString manifest = QDir(project->dir).filePath(QStringLiteral("project.json"));
    // Role reads use the loaded snapshot; only explicit refresh/publication
    // reads the manifest again. No disk access is needed to parse it here.
    QVERIFY(QFile::rename(manifest, manifest + QStringLiteral(".held")));
    QCOMPARE(project->thumbnailUrl(), expected);
    QVERIFY(QFile::rename(manifest + QStringLiteral(".held"), manifest));
}

QTEST_GUILESS_MAIN(tst_RecordingProject)
#include "tst_RecordingProject.moc"
