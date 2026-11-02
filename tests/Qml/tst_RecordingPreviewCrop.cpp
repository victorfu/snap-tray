#include <QtTest/QtTest>
#include <QFileInfo>
#include <QFile>
#include <QScopeGuard>

#include "qml/RecordingPreviewBackend.h"
#include "utils/VideoCropGeometry.h"
#include "recording/WindowTimeline.h"
#include "recording/WindowTimelineSidecar.h"

#include <QSignalSpy>
#include <QTemporaryDir>

class tst_RecordingPreviewCrop : public QObject
{
    Q_OBJECT

private slots:
    void cropIgnoredUntilVideoSizeKnown();
    void setCropRectNormalizes();
    void fullFrameCropClears();
    void clearCropResets();
    void setCropFromViewMapsToVideoPixels();
    void cropRectInViewFallsBackToContent();
    void videoSizeChangeRenormalizes();
    void minCropSideMatchesGeometry();
    void windowLookupWithoutSidecar();
    void windowLookupMapsToView();
    void sidecarFrameSizeMismatchDisablesTimeline();
    void corruptSidecarIsIgnored();
    void timeTextRoundTrips_data();
    void timeTextRoundTrips();
    void trimInputValidates();
    void aspectPresetLocksAppliedCrop();
    void exactInputIsLegalized();
    void undoRedoCoversCropAndTrim();
    void resetIsUndoable();
    void exportNeedsRoomForASecondFile();
    void exportRoomFollowsTheTrimmedLength();
};

namespace {
QString writeTimeline(const QTemporaryDir& dir, const QSize& frameSize)
{
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(frameSize);
    SnapTray::WindowSample code;
    code.windowId = 1;
    code.rect = QRect(200, 100, 400, 200);
    code.ownerApp = QStringLiteral("Code");
    SnapTray::WindowSample safari;
    safari.windowId = 2;
    safari.rect = QRect(0, 0, 800, 400);
    safari.z = 1;
    safari.ownerApp = QStringLiteral("Safari");
    timeline.append(0, {code, safari});
    timeline.append(1000, {safari}); // Code closes at 1 s
    [&] { QVERIFY(SnapTray::WindowTimelineSidecar::write(video, timeline)); }();
    return video;
}
} // namespace

void tst_RecordingPreviewCrop::cropIgnoredUntilVideoSizeKnown()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.setCropRect(QRect(100, 100, 640, 360));
    QVERIFY(!backend.hasCrop());
    QCOMPARE(backend.cropRect(), QRect());
}

void tst_RecordingPreviewCrop::setCropRectNormalizes()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::cropRectChanged);
    backend.setCropRect(QRect(101, 51, 641, 361));
    QCOMPARE(backend.cropRect(), QRect(100, 50, 642, 362));
    QVERIFY(backend.hasCrop());
    QCOMPARE(spy.count(), 1);
    backend.setCropRect(QRect(101, 51, 641, 361));
    QCOMPARE(spy.count(), 1); // unchanged value does not re-notify
}

void tst_RecordingPreviewCrop::fullFrameCropClears()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(100, 100, 640, 360));
    backend.setCropRect(QRect(0, 0, 1920, 1080));
    QVERIFY(!backend.hasCrop());
}

void tst_RecordingPreviewCrop::clearCropResets()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(100, 100, 640, 360));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::cropRectChanged);
    backend.clearCrop();
    QVERIFY(!backend.hasCrop());
    QCOMPARE(spy.count(), 1);
    backend.clearCrop();
    QCOMPARE(spy.count(), 1);
}

void tst_RecordingPreviewCrop::setCropFromViewMapsToVideoPixels()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    backend.setCropFromView(QRectF(100, 150, 100, 50), QRectF(0, 100, 400, 200));
    QCOMPARE(backend.cropRect(), QRect(200, 100, 200, 100));
}

void tst_RecordingPreviewCrop::cropRectInViewFallsBackToContent()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    const QRectF content(0, 100, 400, 200);
    QCOMPARE(backend.cropRectInView(content), content);
    backend.setCropRect(QRect(200, 100, 200, 100));
    QCOMPARE(backend.cropRectInView(content), QRectF(100, 150, 100, 50));
}

void tst_RecordingPreviewCrop::videoSizeChangeRenormalizes()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(1200, 600, 640, 360));
    backend.updateVideoSize(QSize(1280, 720));
    QCOMPARE(backend.cropRect(), QRect(1200, 600, 80, 120));
}

void tst_RecordingPreviewCrop::minCropSideMatchesGeometry()
{
    // The crop overlay sizes its drafts from this, so it must be the value
    // normalizeCropRect() enforces on commit.
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    QCOMPARE(backend.property("minCropSide").toInt(), SnapTray::VideoCropGeometry::kMinCropSide);
    QCOMPARE(backend.minCropSide(), 64);
}

void tst_RecordingPreviewCrop::windowLookupWithoutSidecar()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    QVERIFY(!backend.hasWindowTimeline());
    QCOMPARE(backend.windowRectInViewAt(QPointF(100, 150), QRectF(0, 100, 400, 200), 0), QRectF());
    QCOMPARE(backend.windowAppAt(QPointF(100, 150), QRectF(0, 100, 400, 200), 0), QString());
}

void tst_RecordingPreviewCrop::windowLookupMapsToView()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(writeTimeline(dir, QSize(800, 400)));
    QVERIFY(backend.hasWindowTimeline());
    backend.updateVideoSize(QSize(800, 400));
    QVERIFY(backend.hasWindowTimeline());
    // The video is drawn at half scale, 100 px down: video (200,100 400x200) is view (100,150 200x100).
    const QRectF content(0, 100, 400, 200);
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), content, 500), QRectF(100, 150, 200, 100));
    QCOMPARE(backend.windowAppAt(QPointF(150, 175), content, 500), QStringLiteral("Code"));
    // Outside Code but inside Safari: the window behind.
    QCOMPARE(backend.windowRectInViewAt(QPointF(20, 120), content, 500), QRectF(0, 100, 400, 200));
    QCOMPARE(backend.windowAppAt(QPointF(20, 120), content, 500), QStringLiteral("Safari"));
    // After Code closed only Safari is there.
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), content, 1500), QRectF(0, 100, 400, 200));
    // Outside the content: nothing.
    QCOMPARE(backend.windowRectInViewAt(QPointF(-10, 175), content, 500), QRectF());
}

void tst_RecordingPreviewCrop::sidecarFrameSizeMismatchDisablesTimeline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(writeTimeline(dir, QSize(800, 400)));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::windowTimelineChanged);
    QVERIFY(backend.hasWindowTimeline());
    backend.updateVideoSize(QSize(1920, 1080)); // a sidecar from some other recording
    QVERIFY(!backend.hasWindowTimeline());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), QRectF(0, 100, 400, 200), 500), QRectF());
}

void tst_RecordingPreviewCrop::corruptSidecarIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    QFile corrupt(SnapTray::WindowTimelineSidecar::pathFor(video));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("nope");
    corrupt.close();
    RecordingPreviewBackend backend(video);
    QVERIFY(!backend.hasWindowTimeline());
}

void tst_RecordingPreviewCrop::timeTextRoundTrips_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<qint64>("ms");
    QTest::newRow("seconds") << QStringLiteral("12") << qint64(12000);
    QTest::newRow("fraction") << QStringLiteral("1.5") << qint64(1500);
    QTest::newRow("m:ss") << QStringLiteral("1:05") << qint64(65000);
    QTest::newRow("m:ss.mmm") << QStringLiteral("0:07.250") << qint64(7250);
    QTest::newRow("h:mm:ss.mmm") << QStringLiteral("1:02:03.004") << qint64(3723004);
    QTest::newRow("spaces") << QStringLiteral("  2:00 ") << qint64(120000);
    QTest::newRow("seconds-over-59-with-minutes") << QStringLiteral("1:75") << qint64(-1);
    QTest::newRow("negative") << QStringLiteral("-1") << qint64(-1);
    QTest::newRow("letters") << QStringLiteral("abc") << qint64(-1);
    QTest::newRow("exponent") << QStringLiteral("1e3") << qint64(-1);
    QTest::newRow("too-many-fields") << QStringLiteral("1:2:3:4") << qint64(-1);
    QTest::newRow("empty") << QString() << qint64(-1);
}

void tst_RecordingPreviewCrop::timeTextRoundTrips()
{
    QFETCH(QString, text);
    QFETCH(qint64, ms);
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    QCOMPARE(backend.parseTime(text), ms);
    if (ms >= 0) {
        QCOMPARE(backend.parseTime(backend.formatPreciseTime(ms)), ms);
    }
    QCOMPARE(backend.formatPreciseTime(65432), QStringLiteral("1:05.432"));
}

void tst_RecordingPreviewCrop::trimInputValidates()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateDuration(10000);
    QVERIFY(backend.setTrimFromInput(1000, 9000));
    QCOMPARE(backend.trimStart(), qint64(1000));
    QCOMPARE(backend.trimEnd(), qint64(9000));
    QVERIFY(backend.hasTrim());
    // Rejected: reversed, too short, beyond the end, negative. Nothing changes.
    QVERIFY(!backend.setTrimFromInput(5000, 4000));
    QVERIFY(!backend.setTrimFromInput(5000, 5050));
    QVERIFY(!backend.setTrimFromInput(1000, 10001));
    QVERIFY(!backend.setTrimFromInput(-1, 5000));
    QCOMPARE(backend.trimStart(), qint64(1000));
    QCOMPARE(backend.trimEnd(), qint64(9000));
    // Up to the end is the same as no end trim.
    QVERIFY(backend.setTrimFromInput(1000, 10000));
    QCOMPARE(backend.trimEnd(), qint64(10000));
    QCOMPARE(backend.trimmedDuration(), qint64(9000));
}

void tst_RecordingPreviewCrop::aspectPresetLocksAppliedCrop()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    const QRectF content(0, 0, 960, 540);  // half scale
    QCOMPARE(backend.cropAspectRatio(), 0.0);
    backend.setCropAspectIndex(4);  // 1:1
    QCOMPARE(backend.cropAspectRatio(), 1.0);
    // The legal size is visible before applying, and applying gives exactly it.
    const QRect legal = backend.legalCropForView(QRectF(100, 100, 301, 200), content);
    QCOMPARE(legal.width(), legal.height());
    backend.setCropFromView(QRectF(100, 100, 301, 200), content);
    QCOMPARE(backend.cropRect(), legal);
    backend.setCropAspectIndex(1);  // original 16:9
    QCOMPARE(backend.cropAspectRatio(), 1920.0 / 1080.0);
    const QRectF fitted = backend.fitViewRectToAspect(QRectF(), content);
    QCOMPARE(fitted, content);  // whole frame already has the original aspect
    backend.setCropAspectIndex(5);  // 9:16
    const QRect portrait = backend.legalCropForView(content, content);
    QCOMPARE(qint64(portrait.width()) * 16, qint64(portrait.height()) * 9);
    backend.setCropAspectIndex(99);  // out of range clamps
    QCOMPARE(backend.cropAspectIndex(), 5);
}

void tst_RecordingPreviewCrop::exactInputIsLegalized()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    const QRectF content(10, 20, 960, 540);
    // Odd values are evened; the view rect maps back to the same video pixels.
    const QRectF view = backend.legalViewRectForVideo(101, 51, 641, 361, content);
    QCOMPARE(backend.legalCropForView(view, content), QRect(100, 50, 642, 362));
    backend.setCropFromView(view, content);
    QCOMPARE(backend.cropRect(), QRect(100, 50, 642, 362));
    // Too small grows to the minimum side; beyond the frame is pulled in.
    QCOMPARE(backend.legalCropForView(backend.legalViewRectForVideo(5, 5, 4, 4, content), content).size(),
             QSize(64, 64));
    QVERIFY(QRect(0, 0, 1920, 1080).contains(
        backend.legalCropForView(backend.legalViewRectForVideo(1900, 1000, 500, 500, content), content)));
}

void tst_RecordingPreviewCrop::undoRedoCoversCropAndTrim()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.updateDuration(10000);
    const QRectF content(0, 0, 1920, 1080);
    QVERIFY(!backend.canUndo());
    QVERIFY(!backend.hasEdits());

    backend.setCropFromView(QRectF(100, 100, 640, 360), content);
    const QRect firstCrop = backend.cropRect();
    QVERIFY(backend.canUndo());
    // Re-reporting the video size renormalizes but is not a user edit.
    backend.updateVideoSize(QSize(1920, 1080));
    backend.undo();
    QVERIFY(!backend.canUndo());
    backend.redo();
    QCOMPARE(backend.cropRect(), firstCrop);
    QVERIFY(backend.setTrimFromInput(1000, 8000));
    backend.toggleTrim();  // clears the trim
    QVERIFY(!backend.hasTrim());
    QVERIFY(backend.hasEdits());

    backend.undo();
    QCOMPARE(backend.trimStart(), qint64(1000));
    QCOMPARE(backend.trimEnd(), qint64(8000));
    backend.undo();
    QVERIFY(!backend.hasTrim());
    QCOMPARE(backend.cropRect(), firstCrop);
    backend.undo();
    QVERIFY(!backend.hasCrop());
    QVERIFY(!backend.canUndo());
    backend.redo();
    backend.redo();
    QCOMPARE(backend.cropRect(), firstCrop);
    QCOMPARE(backend.trimEnd(), qint64(8000));
    QVERIFY(backend.canRedo());

    // Dragging a trim handle is one step when it is committed.
    backend.setTrimStart(2000);
    backend.commitTrimEdit();
    QVERIFY(!backend.canRedo());
    backend.undo();
    QCOMPARE(backend.trimStart(), qint64(1000));
}

void tst_RecordingPreviewCrop::resetIsUndoable()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.updateDuration(10000);
    backend.setCropFromView(QRectF(100, 100, 640, 360), QRectF(0, 0, 1920, 1080));
    QVERIFY(backend.setTrimFromInput(1000, 8000));
    QSignalSpy history(&backend, &RecordingPreviewBackend::editHistoryChanged);
    backend.resetEdits();
    QVERIFY(!backend.hasCrop());
    QVERIFY(!backend.hasTrim());
    QVERIFY(!backend.hasEdits());
    QVERIFY(history.count() > 0);
    backend.undo();
    QVERIFY(backend.hasCrop());
    QVERIFY(backend.hasTrim());
    // Processing blocks history changes, like every other edit.
    backend.m_isProcessing = true;
    backend.redo();
    QVERIFY(backend.hasCrop());
    backend.m_isProcessing = false;
}

void tst_RecordingPreviewCrop::exportNeedsRoomForASecondFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(QByteArray(1024 * 1024, 'x')) > 0);  // a 1 MiB "recording"
    }
    qint64 freeBytes = 10 * 1024 * 1024;
    QString queried;
    RecordingPreviewBackend::freeBytesForPathOverride() = [&](const QString& path) {
        queried = path;
        return freeBytes;
    };
    auto restore = qScopeGuard([] { RecordingPreviewBackend::freeBytesForPathOverride() = {}; });

    RecordingPreviewBackend backend(video);
    backend.updateDuration(10000);
    QSignalSpy saved(&backend, &RecordingPreviewBackend::saveRequested);
    // GIF: refused before any work starts, with a retryable error; nothing is deleted.
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.save();
    QVERIFY(!backend.isProcessing());
    QVERIFY(backend.errorMessage().contains(QStringLiteral("Not enough free disk space")));
    QVERIFY(backend.canRetrySave());
    QCOMPARE(QFileInfo(queried).canonicalFilePath(), QFileInfo(dir.path()).canonicalFilePath());
    QVERIFY(QFileInfo::exists(video));
    QCOMPARE(saved.count(), 0);

    // MP4 with a trim also writes a second file.
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QVERIFY(backend.setTrimFromInput(1000, 9000));
    backend.clearError();
    backend.save();
    QVERIFY(!backend.isProcessing());
    QVERIFY(!backend.errorMessage().isEmpty());

    // Enough room (or unknown room): the export starts.
    freeBytes = -1;
    backend.clearError();
    backend.save();
    QVERIFY(backend.errorMessage().isEmpty());
    QVERIFY(backend.isProcessing());
}

void tst_RecordingPreviewCrop::exportRoomFollowsTheTrimmedLength()
{
    // PR #171 review: a few seconds trimmed from a long recording still
    // needed room for the whole recording, so a recording stopped for low
    // space could not even be cut down to a small clip.
    constexpr qint64 kMiB = 1024 * 1024;
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("long.mp4"));
    {
        QFile file(video);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(1024 * kMiB)); // a sparse 1 GiB "recording"
    }
    RecordingPreviewBackend::freeBytesForPathOverride() = [](const QString&) { return 200 * kMiB; };
    auto restore = qScopeGuard([] { RecordingPreviewBackend::freeBytesForPathOverride() = {}; });
    RecordingPreviewBackend backend(video);
    backend.updateDuration(100000);
    backend.updateVideoSize(QSize(1280, 720));
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);

    // The whole recording re-encoded (a crop) does not fit in 200 MiB...
    backend.setCropRect(QRect(0, 0, 640, 360));
    QVERIFY(backend.hasCrop());
    backend.save();
    QVERIFY(backend.canRetrySave());
    QVERIFY(!backend.isProcessing());
    backend.clearCrop();

    // ...ten seconds of it do.
    QVERIFY(backend.setTrimFromInput(20000, 30000));
    backend.clearError();
    backend.save();
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QVERIFY(backend.isProcessing());
}

QTEST_MAIN(tst_RecordingPreviewCrop)
#include "tst_RecordingPreviewCrop.moc"
