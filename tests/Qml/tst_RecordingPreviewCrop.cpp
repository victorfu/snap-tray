#include <QtTest/QtTest>

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
    void longshotAdjustmentIsTransactional() {
        RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
        backend.updateDuration(5000); backend.updateVideoSize(QSize(1920,1080));
        backend.setTrimStart(500); backend.setTrimEnd(4000);
        backend.setCropRect(QRect(100,100,800,600));
        backend.beginLongshotAdjustment();
        backend.setTrimStart(1000); backend.setTrimEnd(3000); backend.clearCrop();
        backend.cancelLongshotAdjustment();
        QCOMPARE(backend.trimStart(),qint64(500)); QCOMPARE(backend.trimEnd(),qint64(4000));
        QCOMPARE(backend.cropRect(),QRect(100,100,800,600));
        QVERIFY(!backend.longshot()->busy());
    }
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

QTEST_MAIN(tst_RecordingPreviewCrop)
#include "tst_RecordingPreviewCrop.moc"
