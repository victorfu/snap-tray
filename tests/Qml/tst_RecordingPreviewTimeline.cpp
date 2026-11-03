#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include <functional>

#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"

// Cuts and speed changes in the preview (#185) against the real QML.
class tst_RecordingPreviewTimeline : public QObject
{
    Q_OBJECT

private slots:
    void buttonsAndKeysEditTheTimeline();
    void playbackFollowsTheEdit();
    void splitFollowsTheWholeDrag();

private:
    static QQuickView* openPreview(RecordingPreviewBackend& backend);
    static QQuickItem* item(QQuickView* view, const char* name);
    static QQuickItem* delegate(QQuickView* view, const QString& name);
    static bool seek(QQuickView* view, qint64 ms);
};

namespace {
bool writeRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->start(path, QSize(160, 90), 10)) return false;
    QImage frame(QSize(160, 90), QImage::Format_ARGB32);
    for (int i = 0; i < 20; ++i) {
        frame.fill(QColor::fromHsv(i * 18, 200, 200));
        const qint64 before = encoder->framesWritten();
        QElapsedTimer wait;
        wait.start();
        do {
            encoder->writeFrame(frame, i * 100);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (wait.elapsed() < 2000);
    }
    // macOS and Windows finish asynchronously.
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) return false;
    return finishedSpy.first().at(0).toBool();
}
} // namespace

QQuickView* tst_RecordingPreviewTimeline::openPreview(RecordingPreviewBackend& backend)
{
    backend.show();
    QQuickView* view = nullptr;
    const auto find = [&view] {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview")) {
                view = qobject_cast<QQuickView*>(window);
            }
        }
        return view != nullptr;
    };
    if (!QTest::qWaitFor(find, 5000)) return nullptr;
    return QTest::qWaitForWindowExposed(view) ? view : nullptr;
}

QQuickItem* tst_RecordingPreviewTimeline::item(QQuickView* view, const char* name)
{
    return view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(name));
}

QQuickItem* tst_RecordingPreviewTimeline::delegate(QQuickView* view, const QString& name)
{
    // Repeater delegates are only reachable through the visual tree.
    std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* node) -> QQuickItem* {
        if (node->objectName() == name) return node;
        for (QQuickItem* child : node->childItems()) {
            if (QQuickItem* found = walk(child)) return found;
        }
        return nullptr;
    };
    return walk(view->contentItem());
}

bool tst_RecordingPreviewTimeline::seek(QQuickView* view, qint64 ms)
{
    QQuickItem* player = item(view, "previewVideoPlayer");
    if (!player) return false;
    QMetaObject::invokeMethod(player, "pause");
    QMetaObject::invokeMethod(player, "seek", Q_ARG(qint64, ms));
    return QTest::qWaitFor([player, ms] { return qAbs(player->property("position").toLongLong() - ms) <= 100; }, 5000);
}

void tst_RecordingPreviewTimeline::buttonsAndKeysEditTheTimeline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    if (!writeRecording(video)) QSKIP("No native encoder for the fixture recording");
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(backend.duration() > 0, 10000);

    QQuickItem* split = item(view, "previewSplit");
    QQuickItem* cut = item(view, "previewPartCut");
    QQuickItem* speed = item(view, "previewPartSpeed");
    QVERIFY(split && cut && speed);
    QVERIFY(split->isEnabled());
    QVERIFY(!cut->isEnabled()); // nothing to cut out of a single part

    QVERIFY(seek(view, 500));
    QVERIFY(QMetaObject::invokeMethod(split, "clicked"));
    QCOMPARE(backend.timelineSplits().size(), 1);
    QTRY_VERIFY(delegate(view, QStringLiteral("previewTimelineSplit0")));
    QTRY_VERIFY(cut->isEnabled());
    // The playhead is at the start of part 1: cut it.
    QVERIFY(QMetaObject::invokeMethod(cut, "clicked"));
    QVERIFY(backend.timelineEdit().parts[1].deleted);
    QVERIFY(backend.hasTimelineEdits());
    QCOMPARE(backend.outputDuration(), 500);
    QTRY_VERIFY(!speed->isEnabled()); // a cut part has no speed

    // Part 0 at 2x... after two clicks (1x -> 1.5x -> 2x).
    QVERIFY(seek(view, 200));
    QTRY_VERIFY(speed->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(speed, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(speed, "clicked"));
    QCOMPARE(backend.timelineEdit().parts[0].speed, 2.0);
    QCOMPARE(backend.outputDuration(), 250);

    // Keys: S splits at the playhead, Delete cuts the part there.
    QVERIFY(seek(view, 1200));
    view->rootObject()->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_S);
    QCOMPARE(backend.timelineSplits().size(), 2);
    // The part the playhead is in (the seek lands on a frame near 1200 ms).
    const qint64 at = item(view, "previewVideoPlayer")->property("position").toLongLong();
    const int part = backend.timelinePartAt(at);
    const bool wasDeleted = backend.timelineEdit().parts[size_t(part)].deleted;
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(backend.timelineEdit().parts[size_t(part)].deleted, !wasDeleted);
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(backend.timelineEdit().parts[size_t(part)].deleted, wasDeleted);

    // Undo walks back one edit at a time; reset clears them all.
    backend.undo();
    QCOMPARE(backend.timelineEdit().parts[size_t(part)].deleted, !wasDeleted);
    backend.undo();
    backend.undo();
    QCOMPARE(backend.timelineSplits().size(), 1);
    backend.redo();
    QCOMPARE(backend.timelineSplits().size(), 2);
    QVERIFY(backend.hasEdits());
    backend.resetEdits();
    QVERIFY(backend.timelineEdit().isEmpty());
    QVERIFY(!backend.hasTimelineEdits());
    QCOMPARE(backend.outputDuration(), backend.duration());
    backend.requestDiscard();
}

void tst_RecordingPreviewTimeline::playbackFollowsTheEdit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    if (!writeRecording(video)) QSKIP("No native encoder for the fixture recording");
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(backend.duration() > 0, 10000);
    QVERIFY(backend.splitAt(500));
    QVERIFY(backend.splitAt(1500));
    backend.setPartDeleted(1, true);
    backend.setPartSpeed(2, 2.0);
    QCOMPARE(backend.playbackTarget(800), 1500);
    QCOMPARE(backend.playbackTarget(300), 300);
    QCOMPARE(backend.playbackSpeedAt(1700), 2.0);
    QQuickItem* player = item(view, "previewVideoPlayer");
    QVERIFY(player);
    // Playing into the cut part jumps over it, at the next part's speed.
    QVERIFY(seek(view, 400));
    QMetaObject::invokeMethod(player, "play");
    QTRY_VERIFY_WITH_TIMEOUT(player->property("position").toLongLong() >= 1500, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(player->property("playbackRate").toReal(), 2.0, 2000);
    QMetaObject::invokeMethod(player, "pause");

    // PR #199 review: a 4x part asks for more than any player plays. The
    // preview plays it at the player's limit, never re-requesting it in a loop.
    backend.setPartSpeed(2, 4.0);
    QCOMPARE(backend.playbackSpeedAt(1700), 4.0);
    QVERIFY(seek(view, 1600));
    QMetaObject::invokeMethod(player, "play");
    QTRY_COMPARE_WITH_TIMEOUT(player->property("playbackRate").toReal(), 2.0, 2000);
    QTest::qWait(300);
    QVERIFY(player->property("playbackRate").toReal() <= 2.0);
    QMetaObject::invokeMethod(player, "pause");

    // Every part cut (cut one, then join it with the kept one: the join
    // keeps the earlier part's cut): playing stops instead of seeking back
    // forever.
    backend.setPartDeleted(0, true);
    backend.removeSplit(1);
    QCOMPARE(backend.playbackTarget(backend.trimStart()), qint64(-1));
    QMetaObject::invokeMethod(player, "play");
    QTRY_VERIFY_WITH_TIMEOUT(!player->property("playing").toBool(), 3000);
    backend.requestDiscard();
}

// PR #199 review: moving a split changed the model on the first move, which
// rebuilt the boundary's delegate and ended the drag there. It now follows
// the pointer to the release and is one undo step.
void tst_RecordingPreviewTimeline::splitFollowsTheWholeDrag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    if (!writeRecording(video)) QSKIP("No native encoder for the fixture recording");
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(backend.duration() > 0, 10000);
    QMetaObject::invokeMethod(item(view, "previewVideoPlayer"), "pause");
    const qint64 start = backend.duration() / 4;
    QVERIFY(backend.splitAt(start));
    QQuickItem* split = nullptr;
    QTRY_VERIFY((split = delegate(view, QStringLiteral("previewTimelineSplit0"))) != nullptr && split->width() > 0);
    QTRY_VERIFY(split->isVisible());
    const QPointF center = split->mapToScene(QPointF(split->width() / 2, split->height() / 2));
    QPoint at = center.toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, at);
    constexpr int kSteps = 8;
    constexpr int kStepPx = 12;
    for (int i = 0; i < kSteps; ++i) {
        at.rx() += kStepPx;
        QTest::mouseMove(view, at);
        QTest::qWait(10);
    }
    // Not changed until the release: the delegate under the pointer stays.
    QCOMPARE(backend.timelineEdit().splits.front(), start);
    QVERIFY(delegate(view, QStringLiteral("previewTimelineSplit0")) == split);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, at);
    const qint64 moved = backend.timelineEdit().splits.front();
    // Most of the way the pointer went, far more than one step.
    QVERIFY2(moved > start, qPrintable(QStringLiteral("%1 -> %2").arg(start).arg(moved)));
    QQuickItem* track = delegate(view, QStringLiteral("previewTimelineSplit0"));
    QVERIFY(track);
    const qreal pxPerMs = qreal(kStepPx * kSteps) / qMax<qint64>(1, moved - start);
    QVERIFY2(moved - start > (kStepPx * 3) / pxPerMs - 1,
             qPrintable(QStringLiteral("moved %1 ms").arg(moved - start)));
    const QPointF after = track->mapToScene(QPointF(track->width() / 2, 0));
    QVERIFY2(qAbs(after.x() - at.x()) <= 3, qPrintable(QStringLiteral("%1 vs %2").arg(after.x()).arg(at.x())));
    // Undo takes the move back (with the split made by this test just before).
    QVERIFY(backend.canUndo());
    backend.undo();
    const auto& splits = backend.timelineEdit().splits;
    QVERIFY(splits.empty() || splits.front() == start);
    backend.requestDiscard();
}

QTEST_MAIN(tst_RecordingPreviewTimeline)
#include "tst_RecordingPreviewTimeline.moc"
