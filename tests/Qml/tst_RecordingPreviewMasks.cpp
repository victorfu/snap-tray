#include <QtTest>

#include <QGuiApplication>
#include <QImageReader>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"

// Masks (#162): burned into every export and into the preview's pixels;
// never silently left out.
namespace {

const QSize kSize(160, 120);
const QRect kSecret(40, 30, 60, 40); // where the "text" is
constexpr int kFrames = 12;
constexpr int kIntervalMs = 100;

// Grey frames with a high-contrast pattern (4 px blocks survive encoding) in kSecret.
QString createSecretRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    if (!encoder->start(path, kSize, 1000 / kIntervalMs)) return encoder->lastError();
    QImage frame(kSize, QImage::Format_ARGB32);
    frame.fill(QColor(128, 128, 128));
    for (int y = kSecret.top(); y <= kSecret.bottom(); ++y)
        for (int x = kSecret.left(); x <= kSecret.right(); ++x)
            frame.setPixelColor(x, y, ((x / 4) + (y / 4)) % 2 ? Qt::white : Qt::black);
    for (int i = 0; i < kFrames; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer wait;
        wait.start();
        do {
            encoder->writeFrame(frame, i * kIntervalMs);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (wait.elapsed() < 2000);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(10000)) return QStringLiteral("encoder did not finish");
    return {};
}

// White pixels anywhere in `rect`: only the secret has any (the background
// is grey and a mask is black), so any white means the secret shows through.
int edges(const QImage& image, QRect rect = QRect())
{
    if (rect.isEmpty()) rect = image.rect();
    rect &= image.rect();
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x) count += qGray(image.pixel(x, y)) > 200 ? 1 : 0;
    return count;
}

int previewEdges(QQuickView* view, QQuickItem* player)
{
    const QImage shot = view->grabWindow();
    const QRectF content = player->property("contentRect").toRectF();
    if (shot.isNull() || content.isEmpty()) return -1;
    const QRectF scene = player->mapRectToScene(content).adjusted(2, 2, -2, -2);
    const qreal dpr = shot.devicePixelRatio();
    const QRect pixels = QRectF(scene.topLeft() * dpr, scene.size() * dpr)
        .toAlignedRect().intersected(shot.rect());
    return pixels.isEmpty() ? -1 : edges(shot, pixels);
}

QImage outputFrame(int format, const QString& path, qint64 ms)
{
    if (format == RecordingPreviewBackend::MP4) {
        auto reader = IVideoFrameReader::create();
        if (!reader || !reader->load(path)) return {};
        return reader->frameAt(ms);
    }
    QImageReader reader(path);
    qint64 start = 0;
    QImage frame;
    while (reader.canRead()) {
        frame = reader.read();
        const int delay = qMax(1, reader.nextImageDelay());
        if (ms < start + delay) break;
        start += delay;
    }
    return frame;
}

class NoFilterTranscoder : public IVideoTranscoder
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest&, const ProgressCallback&) override
    {
        ++calls;
        return {};
    }
    VideoFileProbe probe(const QString&) override { return {}; }
    static inline int calls = 0;
};

} // namespace

class tst_RecordingPreviewMasks : public QObject
{
    Q_OBJECT

private slots:
    void masksAreBurnedIntoEveryOutput_data();
    void masksAreBurnedIntoEveryOutput();
    void exportThatCannotMaskIsRefused();
    void previewShowsTheMaskedPixels();
    void pendingMaskTimesApplyBeforeExport();
    void previewMaskTimesMatchTheExport();
    void overlappingMasksAreAllSelectable();
    void maskDoneAndSwitchingKeepTypedTimesRight();
};

void tst_RecordingPreviewMasks::masksAreBurnedIntoEveryOutput_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<bool>("zoom");
    QTest::newRow("mp4") << int(RecordingPreviewBackend::MP4) << false;
    QTest::newRow("mp4 + zoom") << int(RecordingPreviewBackend::MP4) << true;
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF) << false;
    QTest::newRow("webp + zoom") << int(RecordingPreviewBackend::WebP) << true;
}

void tst_RecordingPreviewMasks::masksAreBurnedIntoEveryOutput()
{
    QFETCH(int, format);
    QFETCH(bool, zoom);
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    const QString fixture = createSecretRecording(input);
    QVERIFY2(fixture.isEmpty(), qPrintable(fixture));

    RecordingPreviewBackend backend(input);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(kSize);
    backend.updateDuration(kFrames * kIntervalMs);
    if (!backend.masksAvailable()) QSKIP("This platform's MP4 transcoder cannot draw masks yet");
    backend.setCropRect(QRect(20, 10, 120, 100));
    backend.setTrimStart(100);
    QCOMPARE(backend.addMask(), 0);
    // A mask over the secret, from 500 to 800 ms; the corner rounds outward.
    backend.setMaskRectFromView(0, QRectF(kSecret).adjusted(-0.5, -0.5, 0.5, 0.5), QRectF(QPointF(0, 0), QSizeF(kSize)));
    QVERIFY(backend.setMaskRange(0, 500, 800));
    if (zoom) {
        QCOMPARE(backend.addZoomAt(100), 0);
        QVERIFY(backend.setZoomRange(0, 100, 1100));
        backend.setZoomScale(0, 1.5);
        backend.setZoomFocusFromView(0, QPointF(kSecret.center()), QRectF(QPointF(0, 0), QSizeF(kSize)));
    }
    QSignalSpy saved(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(saved.count() > 0 || !backend.isProcessing(), 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(saved.count(), 1);
    const QString output = saved.first().at(0).toString();

    // Frame by frame, in source time: shown before, masked from one frame
    // early (conservative) through the end, shown again after.
    const struct { qint64 sourceMs; bool masked; } frames[] = {
        {200, false}, {300, false}, {400, true}, {500, true}, {600, true}, {800, true}, {900, false},
    };
    for (const auto& f : frames) {
        const QImage frame = outputFrame(format, output, f.sourceMs - 100);
        QVERIFY(!frame.isNull());
        const int found = edges(frame);
        if (f.masked) {
            QVERIFY2(found == 0, qPrintable(QStringLiteral("%1 ms: %2 edges leak").arg(f.sourceMs).arg(found)));
        } else {
            QVERIFY2(found > 50, qPrintable(QStringLiteral("%1 ms: pattern missing (%2)").arg(f.sourceMs).arg(found)));
        }
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewMasks::exportThatCannotMaskIsRefused()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    auto restore = qScopeGuard([] { RecordingPreviewBackend::transcoderFactoryOverride() = {}; });
    RecordingPreviewBackend::transcoderFactoryOverride() = [] {
        return std::unique_ptr<IVideoTranscoder>(new NoFilterTranscoder);
    };
    RecordingPreviewBackend backend(input);
    backend.updateVideoSize(kSize);
    backend.updateDuration(kFrames * kIntervalMs);
    // Masks made for a GIF, then MP4 chosen on a system that cannot draw them.
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QCOMPARE(backend.addMask(), 0);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QVERIFY(!backend.masksAvailable());
    QSignalSpy saved(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QVERIFY(!backend.isProcessing());
    QCOMPARE(saved.count(), 0);
    QVERIFY(backend.errorMessage().contains(QStringLiteral("Nothing was exported")));
    QVERIFY(QFileInfo::exists(input)); // the original is untouched

    // Even past the save check, the export job refuses a transcoder that
    // would ignore the masks; it never reaches transcode().
    NoFilterTranscoder::calls = 0;
    backend.clearError();
    backend.performTranscode(false);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 10000);
    QCOMPARE(NoFilterTranscoder::calls, 0);
    QCOMPARE(saved.count(), 0);
    QVERIFY(!backend.errorMessage().isEmpty());
}

void tst_RecordingPreviewMasks::previewShowsTheMaskedPixels()
{
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    RecordingPreviewBackend backend(input);
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
    auto* player = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewVideoPlayer"));
    QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    QMetaObject::invokeMethod(player, "pause");
    // Measure the picture only: the paused play button's glyph and the
    // "no audio" notice (shown without an audio device, as on CI) are white.
    // Hiding a child does not remove its QML visibility binding: a late
    // playback state change can show it again. Hide the unbound parent.
    auto* chrome = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewStageChrome"));
    QVERIFY(chrome);
    chrome->setVisible(false);
    // Native loading can change playback state after the first pause. The
    // pixel measurement must stay free of stage controls across that change.
    QMetaObject::invokeMethod(player, "play");
    QTRY_VERIFY(player->property("playing").toBool());
    QMetaObject::invokeMethod(player, "pause");
    QTRY_VERIFY(!player->property("playing").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(previewEdges(view, player) > 50, 10000);

    if (!backend.masksAvailable()) QSKIP("This platform's MP4 transcoder cannot draw masks yet");
    QCOMPARE(backend.addMask(), 0);
    backend.setMaskRectFromView(0, QRectF(kSecret), QRectF(QPointF(0, 0), QSizeF(kSize)));
    // Measure the picture, not the editor: a selected mask draws an outline
    // and handle in the accent colour, which can be light (CI's theme).
    backend.setSelectedMask(-1);
    // The real frame is re-processed at once: no pattern left in the video area.
    QTRY_COMPARE_WITH_TIMEOUT(previewEdges(view, player), 0, 5000);
    // Undo takes the mask away again.
    backend.undo();
    QTRY_VERIFY_WITH_TIMEOUT(previewEdges(view, player) > 50, 5000);
    backend.requestDiscard();
}

void tst_RecordingPreviewMasks::pendingMaskTimesApplyBeforeExport()
{
    // PR #177 review: a mask end typed but not confirmed (Save does not take
    // focus) was ignored, so the export used the shorter range and showed
    // what the user meant to hide.
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    RecordingPreviewBackend backend(input);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    // Save stops at the free-space check: reaching it offers a retry.
    RecordingPreviewBackend::freeBytesForPathOverride() = [](const QString&) { return qint64(0); };
    auto restore = qScopeGuard([] { RecordingPreviewBackend::freeBytesForPathOverride() = {}; });
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
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    QCOMPARE(backend.addMask(), 0);
    QVERIFY(backend.setMaskRange(0, 0, 300));
    backend.setSelectedMask(0);
    auto* panel = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewMaskPanel"));
    auto* end = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewMaskEndField"));
    QVERIFY(panel && end);
    QTRY_VERIFY(end->isVisible());

    // Every control stays reachable at the 640 px minimum width.
    view->resize(640, 480);
    QTRY_VERIFY([&] {
        for (QQuickItem* child : panel->childItems()) {
            if (!child->isVisible() || child->width() <= 0) continue;
            const QRectF r = child->mapRectToScene(QRectF(0, 0, child->width(), child->height()));
            if (r.left() < 0 || r.right() > view->width()) return false;
        }
        return true;
    }());

    const auto maskEnd = [&backend] { return backend.masks().at(0).toMap().value(QStringLiteral("end")).toLongLong(); };
    // Unreadable: nothing is exported, the field says why.
    end->setProperty("text", QStringLiteral("nonsense"));
    end->setProperty("edited", true);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "saveWithCrop"));
    QVERIFY(!backend.canRetrySave());
    QVERIFY(end->property("invalid").toBool());
    QCOMPARE(maskEnd(), qint64(300));

    end->setProperty("text", backend.formatPreciseTime(900));
    end->setProperty("edited", true);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "saveWithCrop"));
    QCOMPARE(maskEnd(), qint64(900));
    QVERIFY(!end->property("edited").toBool());
    QVERIFY(backend.canRetrySave());
    backend.requestDiscard();
}

void tst_RecordingPreviewMasks::previewMaskTimesMatchTheExport()
{
    // PR #177 review: the preview ended time-limited masks up to 200 ms late
    // (to cover a position running ahead of the frame), so a frame shown
    // masked could be exported unmasked. With each frame's own time, the
    // preview masks exactly the frames the export does.
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    RecordingPreviewBackend backend(input);
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
    auto* player = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewVideoPlayer"));
    QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    QMetaObject::invokeMethod(player, "pause");
    // Hiding a child does not remove its QML visibility binding: a late
    // playback state change can show it again. Hide the unbound parent.
    auto* chrome = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewStageChrome"));
    QVERIFY(chrome);
    chrome->setVisible(false);
    if (!backend.masksAvailable()) QSKIP("This platform's MP4 transcoder cannot draw masks yet");
    QCOMPARE(backend.addMask(), 0);
    backend.setMaskRectFromView(0, QRectF(kSecret), QRectF(QPointF(0, 0), QSizeF(kSize)));
    QVERIFY(backend.setMaskRange(0, 0, 800));
    backend.setSelectedMask(-1);
    QImage frame(kSize, QImage::Format_ARGB32);
    frame.fill(QColor(128, 128, 128));
    for (int y = kSecret.top(); y <= kSecret.bottom(); ++y)
        for (int x = kSecret.left(); x <= kSecret.right(); ++x)
            frame.setPixelColor(x, y, ((x / 4) + (y / 4)) % 2 ? Qt::white : Qt::black);
    const auto show = [&](qint64 ms) {
        return QMetaObject::invokeMethod(player, "onFrameReady", Qt::DirectConnection,
                                         Q_ARG(QImage, frame), Q_ARG(qint64, ms));
    };
    // Inside the mask's time: covered.
    QVERIFY(show(700));
    QTRY_COMPARE_WITH_TIMEOUT(previewEdges(view, player), 0, 5000);
    // After it: the export shows this frame, so the preview does too.
    QVERIFY(show(900));
    QTRY_VERIFY_WITH_TIMEOUT(previewEdges(view, player) > 50, 5000);
    backend.requestDiscard();
}

namespace {
QQuickView* openPreviewFor(RecordingPreviewBackend& backend)
{
    backend.show();
    QQuickView* view = nullptr;
    QTest::qWaitFor([&view] {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview"))
                view = qobject_cast<QQuickView*>(window);
        }
        return view != nullptr;
    }, 5000);
    return view && QTest::qWaitForWindowExposed(view) ? view : nullptr;
}
} // namespace

void tst_RecordingPreviewMasks::overlappingMasksAreAllSelectable()
{
    // PR #177 review: two whole-recording masks share one timeline bar, and
    // the later one covered the earlier, which could not be selected again.
    // Clicking where masks overlap moves through them in turn.
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    RecordingPreviewBackend backend(input);
    QQuickView* view = openPreviewFor(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    if (!backend.masksAvailable()) QSKIP("This platform's MP4 transcoder cannot draw masks yet");
    QCOMPARE(backend.addMask(), 0);
    QCOMPARE(backend.addMask(), 1);
    backend.setSelectedMask(-1);
    // Repeater delegates are only reachable through the visual tree.
    std::function<QQuickItem*(QQuickItem*)> walk = [&walk](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QLatin1String("previewMaskSegment1")) return item;
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = walk(child)) return found;
        }
        return nullptr;
    };
    QQuickItem* top = nullptr;
    QTRY_VERIFY((top = walk(view->rootObject())) != nullptr);
    QTRY_VERIFY(top->width() > 10);
    const QPoint at = top->mapToScene(QPointF(top->width() / 2, top->height() / 2)).toPoint();
    QList<int> seen;
    for (int i = 0; i < 3; ++i) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, at);
        seen << backend.selectedMask();
    }
    QCOMPARE(seen, (QList<int>{1, 0, -1}));
    backend.requestDiscard();
}

void tst_RecordingPreviewMasks::maskDoneAndSwitchingKeepTypedTimesRight()
{
    // PR #177 review: Done cleared the selection without applying a typed
    // end, and Save then skipped the hidden fields, exporting the shorter
    // mask. Done applies typed times (staying open when invalid), and
    // selecting another mask drops text that was not applied.
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("recording.mp4"));
    QVERIFY(createSecretRecording(input).isEmpty());
    RecordingPreviewBackend backend(input);
    QQuickView* view = openPreviewFor(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    if (!backend.masksAvailable()) QSKIP("This platform's MP4 transcoder cannot draw masks yet");
    QCOMPARE(backend.addMask(), 0);
    QCOMPARE(backend.addMask(), 1);
    QVERIFY(backend.setMaskRange(0, 0, 300));
    QVERIFY(backend.setMaskRange(1, 500, 700));
    backend.setSelectedMask(0);
    auto* end = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewMaskEndField"));
    auto* done = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("previewMaskDone"));
    QVERIFY(end && done);
    QTRY_VERIFY(end->isVisible());
    const auto maskEnd = [&backend](int i) { return backend.masks().at(i).toMap().value(QStringLiteral("end")).toLongLong(); };

    // Unreadable: Done keeps the editor open, nothing changes.
    end->setProperty("text", QStringLiteral("nonsense"));
    end->setProperty("edited", true);
    QVERIFY(QMetaObject::invokeMethod(done, "clicked"));
    QCOMPARE(backend.selectedMask(), 0);
    QVERIFY(end->property("invalid").toBool());

    // Switching drops it: it never reaches the other mask.
    backend.setSelectedMask(1);
    QTRY_COMPARE(end->property("text").toString(), backend.formatPreciseTime(700));
    QVERIFY(!end->property("edited").toBool());
    QVERIFY(QMetaObject::invokeMethod(done, "clicked"));
    QCOMPARE(maskEnd(1), qint64(700));

    // A valid typed end: Done applies it.
    backend.setSelectedMask(0);
    QTRY_VERIFY(end->isVisible());
    end->setProperty("text", backend.formatPreciseTime(900));
    end->setProperty("edited", true);
    QVERIFY(QMetaObject::invokeMethod(done, "clicked"));
    QCOMPARE(maskEnd(0), qint64(900));
    QCOMPARE(backend.selectedMask(), -1);
    backend.requestDiscard();
}

QTEST_MAIN(tst_RecordingPreviewMasks)
#include "tst_RecordingPreviewMasks.moc"
