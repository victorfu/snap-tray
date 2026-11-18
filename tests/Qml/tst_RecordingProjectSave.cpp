#include <QtTest>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)
#include <QDir>
#include <QProcess>
#include <QMessageBox>
#include <QPushButton>
#include "recording/RecordingDraft.h"
#include "recording/RecordingSession.h"
#include <QFile>
#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThreadPool>

#include "qml/RecordingPreviewBackend.h"
#include "qml/RecordingFilmstrip.h"
#include <QPainter>
#include <QQuickView>
#include "recording/RecordingProject.h"
#include "settings/RecordingSettingsManager.h"

using namespace SnapTray;

namespace {
bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QString backgroundPath(const QJsonObject& recipe)
{
    return recipe["edit"].toObject()["appearance"].toObject()["imagePath"].toString();
}

QImage background()
{
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    return image;
}
}

class tst_RecordingProjectSave : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }
    void init()
    {
        m_keepProjects = RecordingSettingsManager::instance().keepProjects();
        m_cleanupDays = RecordingSettingsManager::instance().projectCleanupDays();
        RecordingSettingsManager::instance().setKeepProjects(true);
        RecordingSettingsManager::instance().setProjectCleanupDays(0);
    }
    void cleanup()
    {
        QThreadPool::globalInstance()->waitForDone();
        RecordingProjects::setRootForTests({});
        RecordingSettingsManager::instance().setKeepProjects(m_keepProjects);
        RecordingSettingsManager::instance().setProjectCleanupDays(m_cleanupDays);
    }
    void draftCrashWriter();
    void recoverCommittedDraft_data();
    void recoverCommittedDraft();
    void draftFailureKeepsPreviousCommit();
    void explicitProjectSaveClearsDraftOnlyAfterCommit();
    void missingBackgroundKeepsPreviewRetryable();
    void readableHeaderDoesNotMakeACompleteImage();
    void failedSave_data();
    void failedSave();
    void inactiveImageDoesNotBlockSave_data();
    void inactiveImageDoesNotBlockSave();
    void projectOwnsReadableBackground_data();
    void projectOwnsReadableBackground();
private:
    static void handOff(RecordingPreviewBackend& backend, const QString& output)
    {
        backend.m_awaitingSave = true;
        backend.m_isProcessing = true;
        backend.m_pendingSaveOutput = output;
    }
    bool m_keepProjects = false;
    int m_cleanupDays = 0;
};

void tst_RecordingProjectSave::draftCrashWriter()
{
    const QString source = qEnvironmentVariable("SNAPTRAY_TEST_DRAFT_SOURCE");
    if (source.isEmpty()) QSKIP("Run as the crash-test child");
    RecordingPreviewBackend backend(source);
    if (qEnvironmentVariableIsSet("SNAPTRAY_TEST_DRAFT_PROJECT")) {
        const auto project = RecordingProjects::load(QFileInfo(source).absolutePath());
        QVERIFY(project);
        backend.setProject(*project);
    }
    backend.show();
    QTRY_COMPARE(backend.videoSize(), QSize(160, 120));
    QTRY_VERIFY(backend.duration() > 1000 && !backend.projectLoading());
    QVERIFY(!backend.m_draftIdentity.isEmpty());
    auto state = backend.currentEditState();
    state.crop = QRect(16, 12, 124, 88);
    state.trimStart = 100;
    state.timeline.splits = {1000};
    state.timeline.parts = {{false, 2.0, true}, {true, 1.0, true}};
    state.redaction.masks.push_back({QRect(40, 30, 60, 40), 200, 900});
    state.audio.microphoneGainDb = -6;
    state.appearance.enabled = true;
    state.appearance.background = RecordingAppearance::Background::Image;
    state.appearance.imagePath = qEnvironmentVariable("SNAPTRAY_TEST_DRAFT_IMAGE");
    backend.m_restoringProject = true;
    backend.applyEditState(state);
    backend.m_restoringProject = false;
    backend.recordEdit();
    QTRY_VERIFY(RecordingDraft::exists(source));
    // The parent terminates this process without running preview teardown.
    QTest::qWait(30000);
    QFAIL("Parent did not terminate the writer");
}

void tst_RecordingProjectSave::recoverCommittedDraft_data()
{
    QTest::addColumn<bool>("project");
    QTest::newRow("new session") << false;
    QTest::newRow("existing project") << true;
}

void tst_RecordingProjectSave::recoverCommittedDraft()
{
    QFETCH(bool, project);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    QString source = dir.filePath("recording.mp4");
    const QString fixture = QFINDTESTDATA("../Video/fixtures/edit-vfr.mp4");
    QVERIFY(!fixture.isEmpty());
    QVERIFY(QFile::copy(fixture, source));
    std::optional<RecordingProject> saved;
    if (project) {
        saved = RecordingProjects::create(source, false, RecordingRecipe::toJson({}, {}), {}, 5000, {});
        QVERIFY(saved);
        source = saved->videoPath();
    } else {
        RecordingSessionInfo session;
        session.videoPath = source;
        session.sessionId = QStringLiteral("crash-recovery");
        session.format = QStringLiteral("mp4");
        QVERIFY(RecordingSession::write(session));
    }
    const QString image = dir.filePath("external.png");
    QVERIFY(background().save(image));
    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("SNAPTRAY_TEST_DRAFT_SOURCE", source);
    env.insert("SNAPTRAY_TEST_DRAFT_IMAGE", image);
    if (project) env.insert("SNAPTRAY_TEST_DRAFT_PROJECT", "1");
    child.setProcessEnvironment(env);
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("draftCrashWriter")});
    QVERIFY(child.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(RecordingDraft::exists(source) || child.state() == QProcess::NotRunning, 15000);
    QVERIFY2(RecordingDraft::exists(source), child.readAll().constData());
    child.kill();
    QVERIFY(child.waitForFinished());
    QVERIFY(QFile::remove(image));
    if (saved) {
        QCOMPARE(RecordingProjects::load(saved->dir)->recipe, saved->recipe);
        QVERIFY(saved->thumbnailUrl().isEmpty());
        QCOMPARE(RecordingProjects::removeOlderThan(1, QDateTime::currentDateTimeUtc().addDays(4)), 0);
    }
    RecordingPreviewBackend restored(source);
    if (saved) restored.setProject(*saved);
    QTimer answer;
    bool prompted = false;
    connect(&answer, &QTimer::timeout, this, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!box) return;
        for (auto* button : box->buttons()) {
            if (box->buttonRole(button) == QMessageBox::AcceptRole) {
                prompted = !restored.m_view;
                button->click();
                return;
            }
        }
        box->reject();
    });
    answer.start(25);
    restored.show();
    QTRY_VERIFY_WITH_TIMEOUT(prompted, 5000);
    answer.stop();
    QTRY_COMPARE(restored.videoSize(), QSize(160, 120));
    QTRY_VERIFY(restored.duration() > 1000 && !restored.projectLoading());
    const auto state = restored.currentEditState();
    QCOMPARE(state.crop, QRect(16, 12, 124, 88));
    QCOMPARE(state.trimStart, 100);
    QCOMPARE(state.timeline.splits, std::vector<qint64>{1000});
    QCOMPARE(state.timeline.parts.front().speed, 2.0);
    QVERIFY(state.timeline.parts.back().deleted);
    QCOMPARE(state.redaction.masks.size(), size_t(1));
    QCOMPARE(state.redaction.masks.front().rect, QRect(40, 30, 60, 40));
    QCOMPARE(state.audio.microphoneGainDb, -6);
    QCOMPARE(QImage(state.appearance.imagePath).pixelColor(0, 0), QColor(Qt::magenta));
    auto* strip = restored.m_view->rootObject()->findChild<RecordingFilmstrip*>();
    QVERIFY(strip);
    // The restored fixture is solid red through this interval. Black pixels
    // in the actual video lane must therefore come from the recovered mask.
    const auto maskedTileVisible = [strip] {
        QImage surface(strip->size().toSize(), QImage::Format_ARGB32_Premultiplied);
        if (surface.isNull()) return false;
        surface.fill(Qt::transparent);
        QPainter painter(&surface);
        strip->paint(&painter);
        painter.end();
        for (int x = surface.width() / 5; x < surface.width() / 2; ++x)
            if (surface.pixelColor(x, surface.height() / 2) == QColor(Qt::black)) return true;
        return false;
    };
    QTRY_VERIFY(maskedTileVisible());
    restored.undo();
    QVERIFY(restored.flushDraft());
    QVERIFY(!RecordingDraft::exists(source));
    restored.setCropRect(QRect(20, 20, 120, 80));
    restored.recordEdit();
    restored.writeDraft();
    QVERIFY(restored.clearDraft());
    QCoreApplication::processEvents();
    QVERIFY(!RecordingDraft::exists(source));
}

void tst_RecordingProjectSave::draftFailureKeepsPreviousCommit()
{
    QTemporaryDir dir;
    const QString source = dir.filePath("source.mp4");
    QVERIFY(writeFile(source, "source"));
    RecordingPreviewBackend backend(source);
    backend.m_draftIdentity = "identity";
    backend.updateVideoSize({320, 240});
    backend.setDraftBaseline();
    backend.setCropRect(QRect(20, 20, 120, 80));
    backend.recordEdit();
    QVERIFY(backend.flushDraft());
    const auto committed = readFile(RecordingDraft::pathFor(source));
    QVERIFY(!committed.isEmpty());
#ifdef Q_OS_UNIX
    QVERIFY(!(QFileInfo(RecordingDraft::pathFor(source)).permissions()
              & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
#endif
    backend.m_appearance.enabled = true;
    backend.m_appearance.background = RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = dir.filePath("missing.png");
    backend.recordEdit();
    QVERIFY(!backend.flushDraft());
    QVERIFY(backend.canRetryDraft());
    QCOMPARE(readFile(RecordingDraft::pathFor(source)), committed);
    QVERIFY(background().save(backend.m_appearance.imagePath));
    backend.retryDraft();
    QVERIFY(backend.finishDraftWrite());
    QVERIFY(!backend.canRetryDraft());
    QVERIFY(RecordingDraft::read(source, "identity", dir.path()));
    QVERIFY(!RecordingDraft::read(source, "other", dir.path()));
    const QByteArray good = readFile(RecordingDraft::pathFor(source));
    auto newer = QJsonDocument::fromJson(good).object();
    newer["version"] = 300;
    QVERIFY(writeFile(RecordingDraft::pathFor(source), QJsonDocument(newer).toJson()));
    QVERIFY(!RecordingDraft::read(source, "identity", dir.path()));
    QVERIFY(writeFile(RecordingDraft::pathFor(source), good));
    QVERIFY(writeFile(source, "replaced"));
    QVERIFY(!RecordingDraft::read(source, "identity", dir.path()));
    QVERIFY(writeFile(RecordingDraft::pathFor(source), "{"));
    QVERIFY(!RecordingDraft::read(source, "identity", dir.path()));
}

void tst_RecordingProjectSave::explicitProjectSaveClearsDraftOnlyAfterCommit()
{
    QTemporaryDir dir;
    const auto wait = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    const QString source = dir.filePath("source.mp4");
    QVERIFY(writeFile(source, "source"));
    auto project = RecordingProjects::create(source, false, RecordingRecipe::toJson({}, {}), {}, 5000, {});
    QVERIFY(project);
    auto lock = RecordingProjects::lockEditor(project->dir);
    QVERIFY(lock);
    QVERIFY(!RecordingProjects::lockEditor(project->dir));
    QVERIFY(!RecordingProjects::remove(*project));
    RecordingPreviewBackend backend(project->videoPath());
    backend.setProject(*project);
    backend.identifyDraft();
    backend.updateVideoSize({320, 240});
    backend.updateDuration(5000);
    backend.setCropRect({20, 20, 120, 80});
    backend.recordEdit();
    QVERIFY(backend.flushDraft());
    const QString draft = RecordingDraft::pathFor(project->videoPath());
    const auto committed = readFile(draft);
    QVERIFY(!committed.isEmpty());
    const QString output = dir.filePath("export.mp4");
    handOff(backend, output);
    RecordingProjects::failNextManifestWriteForTests();
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved, output);
    QCOMPARE(readFile(draft), committed);
    QVERIFY(!backend.m_closeHandled);
    handOff(backend, output);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved, output);
    QVERIFY(backend.m_closeHandled);
    QVERIFY(!QFile::exists(draft));
    const auto saved = RecordingProjects::load(project->dir);
    QVERIFY(saved);
    QCOMPARE(RecordingRecipe::editState(saved->recipe)->crop, QRect(20, 20, 120, 80));
}

void tst_RecordingProjectSave::missingBackgroundKeepsPreviewRetryable()
{
    QTemporaryDir dir;
    const auto waitForThumbnail = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
    QVERIFY(dir.isValid());
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    const QString source = dir.filePath("source.mp4");
    QVERIFY(writeFile(source, "source"));
    RecordingPreviewBackend backend(source);
    backend.m_appearance.enabled = true;
    backend.m_appearance.background = RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = dir.filePath("missing.png");
    QVERIFY(!backend.keepAsProject(dir.filePath("export.mp4")));
    QVERIFY(QFile::exists(source));
    QVERIFY(RecordingProjects::list().isEmpty());
    QCOMPARE(backend.m_videoPath, source);
}

void tst_RecordingProjectSave::readableHeaderDoesNotMakeACompleteImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    QByteArray png;
    QBuffer output(&png);
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(background().save(&output, "PNG"));
    output.close();
    constexpr int kPngSignatureAndHeaderBytes = 33;
    png.truncate(kPngSignatureAndHeaderBytes);
    QBuffer input(&png);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QImageReader reader(&input, "PNG");
    QVERIFY(reader.canRead());
    QVERIFY(reader.read().isNull());
    const QString source = dir.filePath("source.mp4");
    const QString image = dir.filePath("truncated.png");
    QVERIFY(writeFile(source, "source"));
    QVERIFY(writeFile(image, png));
    RecordingPreviewBackend backend(source);
    backend.m_appearance.enabled = true;
    backend.m_appearance.background = RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = image;
    QVERIFY(!backend.keepAsProject("export.mp4"));
    QVERIFY(QFile::exists(source));
    QVERIFY(RecordingProjects::list().isEmpty());
}

void tst_RecordingProjectSave::failedSave_data()
{
    QTest::addColumn<bool>("existing");
    QTest::addColumn<QString>("failure");
    QTest::addColumn<bool>("closeAfterFailure");
    for (bool existing : {false, true}) {
        for (const QString& failure : {QStringLiteral("missing"), QStringLiteral("unreadable"),
                                      QStringLiteral("copy"), QStringLiteral("manifest")}) {
            for (bool closeAfterFailure : {false, true}) {
                const QByteArray name = QStringLiteral("%1-%2-%3").arg(existing ? "update" : "create", failure,
                    closeAfterFailure ? "close" : "cancel-retry").toUtf8();
                QTest::newRow(name.constData()) << existing << failure << closeAfterFailure;
            }
        }
    }
}

void tst_RecordingProjectSave::failedSave()
{
    QFETCH(bool, existing);
    QFETCH(QString, failure);
    QFETCH(bool, closeAfterFailure);
    QTemporaryDir dir;
    const auto waitForThumbnail = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
    QVERIFY(dir.isValid());
    const QString root = dir.filePath("projects");
    RecordingProjects::setRootForTests(root);
    QString source = dir.filePath("source.mp4");
    QVERIFY(writeFile(source, "original recording"));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QVERIFY(writeFile(source + suffix, suffix.toUtf8()));
    }
    const QString image = dir.filePath("external.png");
    QVERIFY(background().save(image));
    std::optional<RecordingProject> oldProject;
    QByteArray oldManifest;
    QString oldBackground;
    if (existing) {
        RecordingPreviewBackend initial(source);
        initial.m_appearance.enabled = true;
        initial.m_appearance.background = RecordingAppearance::Background::Image;
        initial.m_appearance.imagePath = image;
        QVERIFY(initial.keepAsProject("previous-export.mp4"));
        const auto projects = RecordingProjects::list();
        QCOMPARE(projects.size(), 1);
        oldProject = projects.first();
        source = oldProject->videoPath();
        oldBackground = backgroundPath(oldProject->recipe);
        oldManifest = readFile(QDir(oldProject->dir).filePath("project.json"));
    }
    RecordingPreviewBackend backend(source);
    backend.m_project = oldProject;
    backend.m_appearance.enabled = true;
    backend.m_appearance.background = RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = image;
    backend.m_trimStart = 123;
    backend.m_cropRect = QRect(2, 4, 32, 24);
    const QJsonObject edits = backend.currentRecipe();
    bool copyAttempted = false;
    if (failure == "missing") {
        QVERIFY(QFile::remove(image));
    } else if (failure == "unreadable") {
        QVERIFY(writeFile(image, "not a readable image"));
    } else {
        backend.m_copyProjectBackgroundForTests = [&](const QString& from, const QString& to) {
            copyAttempted = true;
            const bool copied = QFile::copy(from, to);
            if (failure == "manifest") RecordingProjects::failNextManifestWriteForTests();
            // A partial copy failure must not leave an unreferenced asset.
            return copied && failure != "copy";
        };
    }
    const QString exported = dir.filePath("export.mp4");
    QVERIFY(writeFile(exported, "already exported"));
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    handOff(backend, exported);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved, exported);
    if (failure == "copy" || failure == "manifest") QVERIFY(copyAttempted);
    QCOMPARE(closed.count(), 0);
    QVERIFY(!backend.m_saved);
    QVERIFY(backend.m_canRetrySave);
    QVERIFY(!backend.isProcessing());
    QVERIFY(backend.errorMessage().contains("Saved to"));
    QCOMPARE(backend.errorMessage().contains("The image could not be read."),
             failure == "missing" || failure == "unreadable");
    QCOMPARE(backend.currentRecipe(), edits);
    QCOMPARE(backend.m_videoPath, source);
    QCOMPARE(readFile(source), QByteArray("original recording"));
    QCOMPARE(readFile(exported), QByteArray("already exported"));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QCOMPARE(readFile(source + suffix), suffix.toUtf8());
    }
    if (existing) {
        QCOMPARE(readFile(QDir(oldProject->dir).filePath("project.json")), oldManifest);
        QCOMPARE(backend.m_project->recipe, oldProject->recipe);
        QCOMPARE(backend.m_project->savedPath, oldProject->savedPath);
        QCOMPARE(backend.m_project->modified, oldProject->modified);
        QCOMPARE(QDir(oldProject->dir).entryList({"background*"}, QDir::Files).size(), 1);
        QVERIFY(QFile::exists(oldBackground));
    } else {
        QVERIFY(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    }
    if (closeAfterFailure) {
        backend.close();
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.first().first().toBool(), false);
        QCOMPARE(discarded.count(), 1);
        // The owner receives the surviving source (and protects project paths).
        QCOMPARE(discarded.first().first().toString(), source);
        QVERIFY(QFile::exists(source));
        QCOMPARE(readFile(exported), QByteArray("already exported"));
        return;
    }
    const QString cancelledOutput = dir.filePath("cancelled.mp4");
    QVERIFY(writeFile(cancelledOutput, "cancelled export"));
    handOff(backend, cancelledOutput);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);
    QVERIFY(!QFile::exists(cancelledOutput));
    QVERIFY(QFile::exists(source));
    QCOMPARE(backend.currentRecipe(), edits);
    QCOMPARE(closed.count(), 0);
    // Repair the failed I/O and retry the same edit state.
    backend.m_copyProjectBackgroundForTests = {};
    QVERIFY(background().save(image));
    handOff(backend, exported);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved, exported);
    QCOMPARE(closed.count(), 1);
    QVERIFY(closed.first().first().toBool());
    QCOMPARE(discarded.count(), 0);
    QVERIFY(backend.m_projectSaveError.isEmpty());
    const auto projects = RecordingProjects::list();
    QCOMPARE(projects.size(), 1);
    const auto project = projects.first();
    const auto restored = RecordingRecipe::editState(project.recipe);
    QVERIFY(restored);
    QCOMPARE(restored->trimStart, qint64(123));
    QCOMPARE(restored->crop, QRect(2, 4, 32, 24));
    QCOMPARE(readFile(project.videoPath()), QByteArray("original recording"));
    for (const QString& suffix : RecordingProjects::sidecarSuffixes()) {
        QCOMPARE(readFile(project.videoPath() + suffix), suffix.toUtf8());
    }
    if (existing) {
        // The previous manifest must remain renderable after a successful
        // update, including the image it owned before the failed save.
        QVERIFY(QFile::exists(oldBackground));
        const auto previousManifest = QJsonDocument::fromJson(
            readFile(QDir(project.dir).filePath(QStringLiteral("manifest.prev.json")))).object();
        const auto previousRecipe = previousManifest.value(QStringLiteral("recipe")).toObject();
        QCOMPARE(backgroundPath(previousRecipe), oldBackground);
        QVERIFY(!QImage(backgroundPath(previousRecipe)).isNull());
    } else QVERIFY(!QFile::exists(source));
    QVERIFY(QFile::remove(image));
    QCOMPARE(QImage(backgroundPath(project.recipe)), background());
    QThreadPool::globalInstance()->waitForDone();
}

void tst_RecordingProjectSave::inactiveImageDoesNotBlockSave_data()
{
    QTest::addColumn<bool>("enabled");
    QTest::addColumn<QString>("asset");
    for (bool enabled : {false, true}) {
        for (const QString& asset : {QStringLiteral("missing"), QStringLiteral("invalid"),
                                    QStringLiteral("copy-failure"), QStringLiteral("available")}) {
            const QByteArray name = QStringLiteral("%1-%2")
                .arg(enabled ? "enabled-solid" : "disabled-image", asset).toUtf8();
            QTest::newRow(name.constData()) << enabled << asset;
        }
    }
}

void tst_RecordingProjectSave::inactiveImageDoesNotBlockSave()
{
    QFETCH(bool, enabled);
    QFETCH(QString, asset);
    QTemporaryDir dir;
    const auto waitForThumbnail = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    const QString source = dir.filePath("source.mp4");
    QVERIFY(writeFile(source, "source"));
    RecordingPreviewBackend backend(source);
    backend.m_appearance.enabled = enabled;
    backend.m_appearance.background = enabled ? RecordingAppearance::Background::Solid : RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = dir.filePath("inactive.png");
    if (asset == "invalid") {
        QVERIFY(writeFile(backend.m_appearance.imagePath, "not an image"));
    } else if (asset != "missing") {
        QVERIFY(background().save(backend.m_appearance.imagePath));
    }
    if (asset == "copy-failure") {
        backend.m_copyProjectBackgroundForTests = [](const QString& from, const QString& to) {
            QFile::copy(from, to);
            return false;
        };
    }
    QVERIFY(backend.keepAsProject("export.mp4"));
    const auto projects = RecordingProjects::list();
    QCOMPARE(projects.size(), 1);
    const auto project = projects.first();
    if (asset == "available") {
        QVERIFY(RecordingProjects::contains(backgroundPath(project.recipe)));
        QVERIFY(QFile::remove(backend.m_appearance.imagePath));
        QCOMPARE(QImage(backgroundPath(project.recipe)), background());
    } else {
        QVERIFY(QDir(project.dir).entryList({"background*"}, QDir::Files).isEmpty());
    }
    QThreadPool::globalInstance()->waitForDone();
}

void tst_RecordingProjectSave::projectOwnsReadableBackground_data()
{
    QTest::addColumn<bool>("symlinkRoot");
    QTest::newRow("regular-root") << false;
#ifdef Q_OS_UNIX
    QTest::newRow("symlink-root") << true;
#endif
}

void tst_RecordingProjectSave::projectOwnsReadableBackground()
{
    QFETCH(bool, symlinkRoot);
    QTemporaryDir dir;
    const auto waitForThumbnail = qScopeGuard([] { QThreadPool::globalInstance()->waitForDone(); });
    if (symlinkRoot) {
        QVERIFY(QDir(dir.path()).mkdir("project-storage"));
        QVERIFY(QFile::link(dir.filePath("project-storage"), dir.filePath("projects")));
    }
    RecordingProjects::setRootForTests(dir.filePath("projects"));
    const QString source = dir.filePath("source.mp4");
    const QString image = dir.filePath("external.png");
    QVERIFY(writeFile(source, "source"));
    QVERIFY(background().save(image));
    RecordingPreviewBackend backend(source);
    backend.m_appearance.enabled = true;
    backend.m_appearance.background = RecordingAppearance::Background::Image;
    backend.m_appearance.imagePath = image;
    QVERIFY(backend.keepAsProject("export.mp4"));
    auto projects = RecordingProjects::list();
    QCOMPARE(projects.size(), 1);
    QVERIFY(QFile::remove(image));
    const auto project = RecordingProjects::load(projects.first().dir);
    QVERIFY(project);
    const auto state = RecordingRecipe::editState(project->recipe);
    QVERIFY(state);
    QCOMPARE(QImage(state->appearance.imagePath), background());
    RecordingPreviewBackend reopened(project->videoPath());
    reopened.setProject(*project);
    reopened.updateVideoSize(QSize(64, 48));
    reopened.updateDuration(1000);
    QCOMPARE(QImage(reopened.m_appearance.imagePath), background());
    // Reusing an owned image does not copy or delete its bytes.
    reopened.m_copyProjectBackgroundForTests = [](const QString&, const QString&) { return false; };
    QVERIFY(reopened.keepAsProject("export-again.mp4"));
    QCOMPARE(QFileInfo(backgroundPath(reopened.m_project->recipe)).canonicalFilePath(),
             QFileInfo(state->appearance.imagePath).canonicalFilePath());
    QVERIFY(!QImage(backgroundPath(reopened.m_project->recipe)).isNull());
    QVERIFY(RecordingProjects::contains(backgroundPath(reopened.m_project->recipe)));
    auto& settings = RecordingSettingsManager::instance();
    const auto remembered = settings.appearance();
    const auto restoreAppearance = qScopeGuard([&] { settings.setAppearance(remembered); });
    const QString previousImage = dir.filePath("remembered.png");
    auto previous = reopened.m_appearance;
    previous.imagePath = previousImage;
    settings.setAppearance(previous.toJson());
    reopened.m_appearance.imagePath = backgroundPath(reopened.m_project->recipe);
    reopened.setAppearanceEnabled(false);
    QCOMPARE(RecordingAppearance::fromJson(settings.appearance()).imagePath, previousImage);
    reopened.setAppearanceEnabled(true);
#ifdef Q_OS_UNIX
    // An external symlink to an owned asset must not become a dependency.
    const QString alias = dir.filePath("outside-link.png");
    QVERIFY(QFile::link(state->appearance.imagePath, alias));
    reopened.m_appearance.imagePath = alias;
    QVERIFY(reopened.keepAsProject("export-via-link.mp4"));
    QVERIFY(QFile::remove(alias));
    QCOMPARE(QImage(backgroundPath(reopened.m_project->recipe)), background());
#endif
    // Losing even an owned asset must be reported, not saved as complete.
    reopened.m_appearance.imagePath = backgroundPath(reopened.m_project->recipe);
    QVERIFY(QFile::remove(reopened.m_appearance.imagePath));
    QVERIFY(!reopened.keepAsProject("export-third.mp4"));
    QThreadPool::globalInstance()->waitForDone();
}

QTEST_MAIN(tst_RecordingProjectSave)
#include "tst_RecordingProjectSave.moc"
