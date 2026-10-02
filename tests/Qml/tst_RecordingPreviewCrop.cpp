#include <QtTest/QtTest>

#include "qml/RecordingPreviewBackend.h"
#include "utils/VideoCropGeometry.h"

#include <QSignalSpy>

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
};

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

QTEST_MAIN(tst_RecordingPreviewCrop)
#include "tst_RecordingPreviewCrop.moc"
