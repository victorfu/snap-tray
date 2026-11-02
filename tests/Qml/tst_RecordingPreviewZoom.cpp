#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include "qml/RecordingPreviewBackend.h"
#include "recording/CursorTimeline.h"
#include "video/IVideoTranscoder.h"

// Zoom editing in the preview (#161): backend rules and the real QML.
namespace {

const QSize kFrame(1920, 1080);

// Clicks in two separate places, five seconds apart.
QString writeClicks(const QTemporaryDir& dir)
{
    const QString video = dir.filePath(QStringLiteral("missing.mp4"));
    SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(video));
    SnapTray::CursorTimelineInfo info;
    info.frameSize = kFrame;
    info.clicksAvailable = true;
    writer.open(info);
    writer.addSample({0, QPoint(300, 300), true, 0});
    writer.addButton({2000, Qt::LeftButton, true, QPoint(300, 300), false});
    writer.addButton({2080, Qt::LeftButton, false, QPoint(300, 300), false});
    writer.addSample({6000, QPoint(1500, 800), true, 0});
    writer.addButton({7000, Qt::LeftButton, true, QPoint(1500, 800), false});
    writer.addButton({7080, Qt::LeftButton, false, QPoint(1500, 800), false});
    writer.finish();
    return video;
}

class NoFilterTranscoder : public IVideoTranscoder
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest&, const ProgressCallback&) override { return {}; }
    VideoFileProbe probe(const QString&) override { return {}; }
};

} // namespace

class tst_RecordingPreviewZoom : public QObject
{
    Q_OBJECT

private slots:
    void autoZoomKeepsTheUsersZooms();
    void withoutPointerDataOnlyManualZoom();
    void rangeEditsStayLegal();
    void formatsThatCannotDrawDisableZoom();
    void previewControls();
    void addZoomNeverChangesExistingZooms();
};

void tst_RecordingPreviewZoom::autoZoomKeepsTheUsersZooms()
{
    QTemporaryDir dir;
    RecordingPreviewBackend backend(writeClicks(dir));
    // GIF draws zooms on every platform (MP4 only where the transcoder can).
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(kFrame);
    backend.updateDuration(10000);
    QVERIFY(backend.autoZoomAvailable());
    QCOMPARE(backend.generateAutoZoom(), 2);
    QCOMPARE(backend.zoomSegments().size(), 2);
    QVERIFY(!backend.zoomSegments().first().toMap().value(QStringLiteral("manual")).toBool());
    // A zoom is a look, not a footage edit, but it is drawn: the export re-encodes.
    QVERIFY(!backend.hasEdits());
    QVERIFY(backend.effectsActive() || !backend.zoomAvailable());

    // The user changes the second one; regenerating replaces only the first.
    backend.setZoomScale(1, 3.0);
    QVERIFY(backend.zoomSegments().at(1).toMap().value(QStringLiteral("manual")).toBool());
    QCOMPARE(backend.generateAutoZoom(), 2);
    QCOMPARE(backend.zoomSegments().size(), 2);
    QCOMPARE(backend.zoomSegments().at(1).toMap().value(QStringLiteral("scale")).toDouble(), 3.0);

    // Regenerating gave the same automatic zoom: no new undo step. Undo and
    // redo walk through zoom changes like crop and trim.
    backend.undo();
    QCOMPARE(backend.zoomSegments().at(1).toMap().value(QStringLiteral("scale")).toDouble(), 2.0);
    backend.redo();
    QCOMPARE(backend.zoomSegments().at(1).toMap().value(QStringLiteral("scale")).toDouble(), 3.0);
    // "Reset Crop and Trim" keeps zooms.
    backend.setTrimStart(500);
    backend.commitTrimEdit();
    backend.resetEdits();
    QCOMPARE(backend.zoomSegments().size(), 2);
}

void tst_RecordingPreviewZoom::withoutPointerDataOnlyManualZoom()
{
    QTemporaryDir dir;
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(kFrame);
    backend.updateDuration(10000);
    QVERIFY(!backend.autoZoomAvailable());
    QCOMPARE(backend.generateAutoZoom(), 0); // nothing invented
    QVERIFY(backend.zoomSegments().isEmpty());
    QVERIFY(backend.zoomAvailable());
    QCOMPARE(backend.addZoomAt(4000), 0);
    QCOMPARE(backend.selectedZoom(), 0);
    // Centred on the picture without a pointer to follow.
    QCOMPARE(backend.zoomTrack().segments().front().focus, QPointF(960, 540));
    QVERIFY(backend.zoomTrack().segments().front().manual);
    backend.removeZoom(0);
    QVERIFY(backend.zoomSegments().isEmpty());
    QCOMPARE(backend.selectedZoom(), -1);
}

void tst_RecordingPreviewZoom::rangeEditsStayLegal()
{
    QTemporaryDir dir;
    RecordingPreviewBackend backend(writeClicks(dir));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(kFrame);
    backend.updateDuration(10000);
    QVERIFY(backend.zoomAvailable());
    // A zoom added at the playhead follows the pointer there.
    QCOMPARE(backend.addZoomAt(1000), 0);
    QCOMPARE(backend.zoomTrack().segments().front().focus, QPointF(300, 300));
    QCOMPARE(backend.addZoomAt(6000), 1);
    QVERIFY(!backend.setZoomRange(0, 1000, 6500)); // into the next one
    QVERIFY(!backend.setZoomRange(0, 1000, 1200)); // too short to see
    QVERIFY(!backend.setZoomRange(5, 0, 1000));    // no such zoom
    QVERIFY(backend.setZoomRange(0, 500, 5000));
    QCOMPARE(backend.zoomTrack().segments().front().endMs, qint64(5000));
}

void tst_RecordingPreviewZoom::formatsThatCannotDrawDisableZoom()
{
    auto restore = qScopeGuard([] { RecordingPreviewBackend::transcoderFactoryOverride() = {}; });
    RecordingPreviewBackend::transcoderFactoryOverride() = [] {
        return std::unique_ptr<IVideoTranscoder>(new NoFilterTranscoder);
    };
    QTemporaryDir dir;
    RecordingPreviewBackend backend(writeClicks(dir));
    backend.updateVideoSize(kFrame);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QVERIFY(!backend.zoomAvailable());
    QVERIFY(!backend.cursorEffectsAvailable());
    QCOMPARE(backend.addZoomAt(1000), -1);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QVERIFY(backend.zoomAvailable()); // drawn in the shared GIF/WebP loop
}

void tst_RecordingPreviewZoom::previewControls()
{
    QTemporaryDir dir;
    RecordingPreviewBackend backend(writeClicks(dir));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.show();
    QQuickView* view = nullptr;
    QVERIFY(QTest::qWaitFor([&view] {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview"))
                view = qobject_cast<QQuickView*>(window);
        }
        return view != nullptr;
    }, 5000));
    QVERIFY(QTest::qWaitForWindowExposed(view));
    backend.updateVideoSize(kFrame);
    backend.updateDuration(10000);
    QVERIFY(backend.zoomAvailable()); // GIF: on every platform
    // Repeater delegates are only reachable through the visual tree.
    std::function<QQuickItem*(QQuickItem*, const QString&)> walk = [&walk](QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = walk(child, name)) return found;
        }
        return nullptr;
    };
    const auto find = [view, &walk](const QString& name) { return walk(view->rootObject(), name); };
    QQuickItem* autoZoom = find(QStringLiteral("previewAutoZoom"));
    QVERIFY(autoZoom && autoZoom->isVisible() && autoZoom->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(autoZoom, "clicked"));
    QCOMPARE(backend.zoomSegments().size(), 2);

    // Bars on the timeline; selecting one swaps the format row for its editor.
    QTRY_VERIFY(find(QStringLiteral("previewZoomSegment1")));
    QQuickItem* panel = find(QStringLiteral("previewZoomPanel"));
    QVERIFY(panel && !panel->isVisible());
    // Every control stays reachable at the 640 px minimum width (#176 review).
    view->resize(640, 480);
    const auto allInside = [view](QQuickItem* row) {
        for (QQuickItem* child : row->childItems()) {
            if (!child->isVisible() || child->width() <= 0) continue;
            const QRectF r = child->mapRectToScene(QRectF(0, 0, child->width(), child->height()));
            if (r.left() < 0 || r.right() > view->width()) return false;
        }
        return true;
    };
    QTRY_VERIFY(allInside(find(QStringLiteral("previewFormatRow"))));
    QQuickItem* video = find(QStringLiteral("previewVideoPlayer"));
    QVERIFY(video);
    const qreal videoHeight = video->height();
    backend.setSelectedZoom(1);
    QTRY_VERIFY(panel->isVisible());
    QTRY_VERIFY(allInside(panel));
    // Swapping the (wrapped) format row for the editor keeps the video still.
    QCOMPARE(video->height(), videoHeight);
    QQuickItem* scale = find(QStringLiteral("previewZoomScale2")); // 2.5x
    QVERIFY(scale);
    QVERIFY(QMetaObject::invokeMethod(scale, "clicked"));
    QCOMPARE(backend.zoomTrack().segments().at(1).scale, 2.5);
    QVERIFY(QMetaObject::invokeMethod(find(QStringLiteral("previewZoomDone")), "clicked"));
    QCOMPARE(backend.selectedZoom(), -1);
    QTRY_VERIFY(!panel->isVisible());
    backend.requestDiscard();
}

void tst_RecordingPreviewZoom::addZoomNeverChangesExistingZooms()
{
    // PR #176 review: adding just before an existing zoom trimmed or deleted it.
    QTemporaryDir dir;
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(kFrame);
    backend.updateDuration(20000);
    QCOMPARE(backend.addZoomAt(5000), 0);
    QVERIFY(backend.setZoomRange(0, 5000, 6000));
    const auto existing = backend.zoomTrack().segments().front();
    // Just before it: fits the free time, the existing zoom stays as it was.
    QCOMPARE(backend.addZoomAt(4300), 0);
    QCOMPARE(backend.selectedZoom(), 0); // the one inserted
    QCOMPARE(backend.zoomTrack().segments().size(), size_t(2));
    QCOMPARE(backend.zoomTrack().segments()[0].startMs, qint64(4300));
    QCOMPARE(backend.zoomTrack().segments()[0].endMs, qint64(5000));
    QCOMPARE(backend.zoomTrack().segments()[1], existing);
    // Too little room, or inside an existing zoom: nothing changes.
    QCOMPARE(backend.addZoomAt(4900), -1);
    QCOMPARE(backend.addZoomAt(5500), -1);
    QCOMPARE(backend.zoomTrack().segments().size(), size_t(2));
    QCOMPARE(backend.zoomTrack().segments()[1], existing);
}

QTEST_MAIN(tst_RecordingPreviewZoom)
#include "tst_RecordingPreviewZoom.moc"
