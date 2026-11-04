#include <QtTest>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QDir>
#include <functional>
#include <QScopeGuard>
#include <QJSValue>
#include <QQmlComponent>
#include "ToolbarStyle.h"
#include "ui/DesignSystem.h"
#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"

class tst_RecordingEditor : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*(ReferenceError|TypeError).*")); }
    void stableLayoutAndExport_data();
    void stableLayoutAndExport();
    void clipAndEffectKeyboardSelection();
    void pendingRangeBlocksSelection();
    void dragIsOneUndoStep();
    void backendSelectionIsExclusive();
    void seekPreservesEditing();
    void openEndedAndTinySegments();
    void customChoicesAndFrameRate();
    void dialogButtonPreservesMouseFocus();
    void switchFollowsUndo();
    void deleteCutsSelectedClip();
    void cutButtonFollowsSelectedClip();
    void adjacentShortEffectsStaySeparate();
    void narrowHeaderKeepsActionsVisible();
private:
    static QQuickView* openPreview(RecordingPreviewBackend& backend);
    static QQuickItem* item(QQuickView* view, const char* name);
    static QQuickItem* delegate(QQuickView* view, const QString& name);
    static bool seek(QQuickView* view, qint64 ms);
    static void evidence(QQuickView* view, const QString& name) {
        const QString path = qEnvironmentVariable("SNAPTRAY_EDITOR_EVIDENCE_DIR");
        if (path.isEmpty()) return;
        QDir().mkpath(path);
        QTest::qWait(150);
        QVERIFY(view->grabWindow().save(path + "/" + name + "-" + QString::fromLatin1(QTest::currentDataTag()) + ".png"));
    }
};
namespace {
bool writeRecording(const QString& path, int fps = 10)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->start(path, QSize(160, 90), fps)) return false;
    QImage frame(QSize(160, 90), QImage::Format_ARGB32);
    for (int i = 0; i < 20; ++i) {
        frame.fill(QColor::fromHsv(i * 18, 200, 200));
        const qint64 before = encoder->framesWritten();
        QElapsedTimer wait;
        wait.start();
        do {
            encoder->writeFrame(frame, i * 1000 / fps);
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

QQuickView* tst_RecordingEditor::openPreview(RecordingPreviewBackend& backend)
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

QQuickItem* tst_RecordingEditor::item(QQuickView* view, const char* name)
{
    return view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(name));
}

QQuickItem* tst_RecordingEditor::delegate(QQuickView* view, const QString& name)
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

bool tst_RecordingEditor::seek(QQuickView* view, qint64 ms)
{
    QQuickItem* player = item(view, "previewVideoPlayer");
    if (!player) return false;
    QMetaObject::invokeMethod(player, "pause");
    QMetaObject::invokeMethod(player, "seek", Q_ARG(qint64, ms));
    return QTest::qWaitFor([player, ms] { return qAbs(player->property("position").toLongLong() - ms) <= 100; }, 5000);
}


void tst_RecordingEditor::stableLayoutAndExport_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("dark") << true;
    QTest::newRow("light") << false;
}

void tst_RecordingEditor::stableLayoutAndExport()
{
    QFETCH(bool, dark);
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QQuickItem* player = item(view, "previewVideoPlayer");
    QQuickItem* inspector = item(view, "editorInspector");
    QVERIFY(player && inspector);
    QTRY_VERIFY(inspector->isVisible());
    const QSizeF size = player->size();
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceOptions"), "clicked"));
    QTRY_VERIFY(item(view, "previewAppearancePanel")->isVisible());
    QCOMPARE(player->size(), size);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSection", Q_ARG(QVariant, "")));
    evidence(view, "editor-wide");
    const int mask = backend.addMask();
    QVERIFY(mask >= 0);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    QCOMPARE(player->size(), size);
    // More lanes never resize the preview.
    QVERIFY(backend.splitAt(1200));
    QVERIFY(backend.addTextOverlay(600) >= 0);
    QVERIFY(backend.setOverlayRange(0, 600, 1400));
    QVERIFY(backend.setMaskRange(mask, 200, 1000));
    backend.setSelectedMask(mask);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    QCOMPARE(player->size(), size);
    evidence(view, "editor-selected");
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewExportOptions"), "clicked"));
    QObject* popup = view->rootObject()->findChild<QObject*>("previewExportPanel");
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    QCOMPARE(backend.selectedMask(), mask); // opening export does not lose the selection
    QCOMPARE(player->size(), size);
    evidence(view, "editor-export");
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(!popup->property("opened").toBool());
    view->resize(640, 480);
    QTRY_VERIFY(view->rootObject()->property("wideEditor").toBool() == false);
    QTRY_VERIFY(inspector->isVisible());
    QVERIFY(player->height() > 100);
    const QRectF inspectorBounds = inspector->mapRectToScene(QRectF(QPointF(), inspector->size()));
    QVERIFY(QRectF(0, 0, 640, 480).contains(inspectorBounds));
    evidence(view, "editor-narrow");
    backend.requestDiscard();
}

void tst_RecordingEditor::pendingRangeBlocksSelection()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    const int first = backend.addMask();
    const int second = backend.addMask();
    QVERIFY(first >= 0 && second >= 0);
    QVERIFY(backend.setMaskRange(first, 0, 1000));
    backend.setSelectedMask(first);
    QQuickItem* field = item(view, "previewMaskStartField");
    QVERIFY(field);
    field->setProperty("text", "invalid");
    field->setProperty("edited", true);
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "selectEffect", Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, "mask"), Q_ARG(QVariant, second)));
    QVERIFY(!result.toBool());
    QCOMPARE(backend.selectedMask(), first);
    QVERIFY(field->property("invalid").toBool());
    field->setProperty("text", "0:00.200");
    QQuickItem* other = nullptr;
    QTRY_VERIFY((other = delegate(view, "previewMaskSegment1")) != nullptr);
    QSignalSpy rendered(view, &QQuickWindow::afterAnimating);
    view->requestUpdate();
    QVERIFY(rendered.wait(2000));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                     other->mapToScene(QPointF(other->width() / 2, other->height() / 2)).toPoint());
    QTRY_COMPARE(backend.selectedMask(), second);
    QCOMPARE(backend.masks().at(first).toMap().value("start").toLongLong(), 200);
    backend.requestDiscard();
}

void tst_RecordingEditor::dragIsOneUndoStep()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    const int mask = backend.addMask();
    QVERIFY(backend.setMaskRange(mask, 200, 700));
    QQuickItem* segment = nullptr;
    QTRY_VERIFY((segment = delegate(view, "previewMaskSegment0")) != nullptr);
    QTRY_VERIFY(segment->width() > 30);
    const QPoint origin = segment->mapToScene(QPointF(segment->width() / 2, 12)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, origin);
    QTest::mouseMove(view, origin + QPoint(40, 0), 30);
    // Backend is unchanged until release.
    QCOMPARE(backend.masks().at(0).toMap().value("start").toLongLong(), 200);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, origin + QPoint(40, 0));
    QTRY_VERIFY(backend.masks().at(0).toMap().value("start").toLongLong() > 200);
    backend.undo();
    QCOMPARE(backend.masks().at(0).toMap().value("start").toLongLong(), 200);
    QCOMPARE(backend.masks().at(0).toMap().value("end").toLongLong(), 700);
    QTRY_VERIFY((segment = delegate(view, "previewMaskSegment0")) != nullptr);
    const QPoint again = segment->mapToScene(QPointF(segment->width() / 2, 12)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, again);
    QTest::mouseMove(view, again + QPoint(40, 0), 30);
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, again + QPoint(40, 0));
    QCOMPARE(backend.masks().at(0).toMap().value("start").toLongLong(), 200);
    backend.requestDiscard();
}

void tst_RecordingEditor::clipAndEffectKeyboardSelection()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(backend.splitAt(1000));
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "selectPart", Q_ARG(QVariant, 1)));
    QTRY_VERIFY(item(view, "previewClipPanel")->isVisible());
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), 1);
    const int overlay = backend.addTextOverlay(0);
    QVERIFY(overlay >= 0);
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), -1);
    QTRY_VERIFY(item(view, "previewOverlayPanel")->isVisible());
    view->rootObject()->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(backend.overlays().size(), 0);
    QVERIFY(!backend.timelineEdit().parts[0].deleted);
    QVERIFY(!backend.timelineEdit().parts[1].deleted);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "undoEdit"));
    QCOMPARE(backend.overlays().size(), 1);
    QCOMPARE(backend.selectedOverlay(), -1);
    backend.setSelectedOverlay(0);
    view->rootObject()->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(backend.selectedOverlay(), -1);
    QVERIFY(!backend.discardConfirmationPending());
    backend.requestDiscard();
}

void tst_RecordingEditor::backendSelectionIsExclusive()
{
    RecordingPreviewBackend backend("missing.mp4");
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(QSize(640, 480));
    backend.updateDuration(10000);
    QVERIFY(backend.addMask() >= 0);
    QVERIFY(backend.addTextOverlay(0) >= 0);
    QCOMPARE(backend.selectedMask(), -1);
    QCOMPARE(backend.selectedOverlay(), 0);
    backend.setSelectedMask(0);
    QCOMPARE(backend.selectedOverlay(), -1);
    QCOMPARE(backend.selectedMask(), 0);
}

void tst_RecordingEditor::seekPreservesEditing()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(seek(view, 0));
    QVERIFY(backend.splitAt(1000));
    const int mask = backend.addMask();
    QVERIFY(mask >= 0);
    QQuickItem* scrubber = item(view, "previewScrubber");
    QVERIFY(scrubber);
    auto trackPoint = [&](qreal fraction) {
        const qreal left = scrubber->property("trackLeft").toReal();
        const qreal width = scrubber->property("trackWidth").toReal();
        return scrubber->mapToScene(QPointF(left + fraction * width, scrubber->property("laneCenterY").toReal())).toPoint();
    };
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.7));
    QCOMPARE(backend.selectedMask(), mask);
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), -1);
    QTRY_VERIFY(item(view, "previewVideoPlayer")->property("position").toLongLong() >= 1200);
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.3));
    QTest::mouseMove(view, trackPoint(0.8), 30);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.8));
    QCOMPARE(backend.selectedMask(), mask);
    // A narrow inspector that was hidden must remain hidden after seeking.
    view->resize(640, 480);
    view->rootObject()->setProperty("inspectorOpen", false);
    QTRY_VERIFY(!item(view, "editorInspector")->isVisible());
    QSignalSpy narrowLayout(view, &QQuickWindow::afterAnimating);
    view->requestUpdate();
    QVERIFY(narrowLayout.wait(2000));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.2));
    QCOMPARE(backend.selectedMask(), mask);
    QVERIFY(!view->rootObject()->property("inspectorOpen").toBool());
    QTRY_VERIFY(item(view, "previewVideoPlayer")->property("position").toLongLong() <= 600);
    // Escape closes the narrow inspector instead of showing Project settings.
    view->rootObject()->setProperty("inspectorOpen", true);
    view->rootObject()->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(backend.selectedMask(), -1);
    QVERIFY(!view->rootObject()->property("inspectorOpen").toBool());
    QVERIFY(!backend.discardConfirmationPending());
    // Crop a real loaded frame, then seek without committing or dropping its draft.
    view->resize(1200, 800);
    QTRY_VERIFY(item(view, "previewCropButton")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewCropButton"), "clicked"));
    QQuickItem* crop = item(view, "previewCropOverlay");
    QVERIFY(crop);
    QTRY_VERIFY(crop->property("editing").toBool());
    QSignalSpy cropLayout(view, &QQuickWindow::afterAnimating);
    view->requestUpdate();
    QVERIFY(cropLayout.wait(2000));
    // The window was just resized: the draft is in picture coordinates, so set it
    // only once the picture's geometry has stopped changing.
    QQuickItem* player = item(view, "previewVideoPlayer");
    QRectF settled;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const QRectF now = player->property("contentRect").toRectF();
        QSignalSpy frame(view, &QQuickWindow::afterAnimating);
        view->requestUpdate();
        frame.wait(500);
        const bool stable = !now.isEmpty() && now == player->property("contentRect").toRectF() && now == settled;
        settled = now;
        return stable;
    }(), 5000);
    const QRectF draft(100, 50, 180, 100);
    crop->setProperty("draftRect", draft);
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.2));
    QTest::mouseMove(view, trackPoint(0.7), 30);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.7));
    QVERIFY(crop->property("editing").toBool());
    QCOMPARE(crop->property("draftRect").toRectF(), draft);
    QVERIFY(!backend.hasCrop());
    QVERIFY(QMetaObject::invokeMethod(crop, "cancel"));
    // Explicit clip selection uses the clicked source time, even before async seek settles.
    QVERIFY(seek(view, 0));
    QTest::mouseDClick(view, Qt::LeftButton, Qt::NoModifier, trackPoint(0.75));
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), 1);
    backend.requestDiscard();
}

void tst_RecordingEditor::openEndedAndTinySegments()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(backend.addMask() >= 0);
    QVERIFY(backend.addTextOverlay(0) >= 0);
    QVERIFY(backend.setOverlayRange(0, 0, -1));
    for (const QString& kind : {QStringLiteral("Mask"), QStringLiteral("Overlay")}) {
        const auto range = [&]() { return (kind == "Mask" ? backend.masks() : backend.overlays()).at(0).toMap(); };
        auto drag = [&](bool rightEdge, int dx) {
            QSignalSpy rendered(view, &QQuickWindow::afterAnimating);
            view->requestUpdate();
            QVERIFY(rendered.wait(2000));
            QQuickItem* segment = delegate(view, "preview" + kind + "Segment0");
            QVERIFY(segment);
            const QPoint from = segment->mapToScene(QPointF(rightEdge ? segment->width() - 2 : 2, 12)).toPoint();
            QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
            QTest::mouseMove(view, from + QPoint(dx, 0), 30);
            QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, from + QPoint(dx, 0));
        };
        QCOMPARE(range().value("end").toLongLong(), -1);
        drag(false, 40);
        QVERIFY(range().value("start").toLongLong() > 0);
        QCOMPARE(range().value("end").toLongLong(), -1);
        drag(false, -40);
        QCOMPARE(range().value("start").toLongLong(), 0);
        QCOMPARE(range().value("end").toLongLong(), -1);
        drag(true, -40);
        QVERIFY2(range().value("end").toLongLong() >= 0, qPrintable(kind));
        QVERIFY(range().value("end").toLongLong() < backend.duration());
    }
    // Enough effects for scrolling: a diagonal segment drag must keep its grab.
    for (int i = 0; i < 6; ++i) QVERIFY(backend.addMask() >= 0);
    QVERIFY(backend.setMaskRange(0, 500, 510));
    backend.setSelectedMask(0);
    QQuickItem* segment = nullptr;
    QTRY_VERIFY((segment = delegate(view, "previewMaskSegment0")) != nullptr);
    QCOMPARE(segment->width(), 8.0);
    QQuickItem* tracks = item(view, "previewEffectTracks");
    QQuickItem* scrubber = item(view, "previewScrubber");
    QVERIFY(tracks && scrubber);
    // Selecting the mask scrolled its lane into view.
    QTRY_VERIFY(QRectF(tracks->mapToScene(QPointF()), tracks->size())
                    .contains(segment->mapToScene(QPointF(4, 12))));
    const qreal scrolledTo = tracks->property("contentY").toReal();
    const QPointF trackOrigin = scrubber->mapToScene(QPointF());
    const QPointF segmentOrigin = segment->mapToScene(QPointF());
    const QPoint from = segment->mapToScene(QPointF(4, 12)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view, from + QPoint(40, 30), 30);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, from + QPoint(40, 30));
    const QVariantMap moved = backend.masks().at(0).toMap();
    QVERIFY(moved.value("start").toLongLong() > 500);
    QCOMPARE(moved.value("end").toLongLong() - moved.value("start").toLongLong(), 10);
    QCOMPARE(tracks->property("contentY").toReal(), scrolledTo);
    tracks->setProperty("contentY", scrolledTo - 60);
    QCOMPARE(scrubber->mapToScene(QPointF()), trackOrigin);
    QCOMPARE(delegate(view, "previewMaskSegment0")->mapToScene(QPointF()).y(), segmentOrigin.y() + 60);
    backend.requestDiscard();
}

void tst_RecordingEditor::customChoicesAndFrameRate()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video, 30));
    RecordingPreviewBackend backend(video);
    backend.setCursorScale(1.25);
    backend.setCursorIdleHide(1250);
    backend.setCursorStillEnd(750);
    backend.setMicrophoneGainDb(2.5);
    backend.setSystemAudioGainDb(-3.5);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    for (const char* name : {"previewPointerSize", "previewPointerIdle", "previewPointerStillEnd",
                             "previewMicrophoneGain", "previewSystemAudioGain"}) {
        QQuickItem* choice = item(view, name);
        QVERIFY2(choice, name);
        QVERIFY2(choice->property("hasCustomValue").toBool(), name);
        QObject* control = choice->findChild<QObject*>("editorChoiceControl");
        QVERIFY(control);
        QCOMPARE(control->property("currentText").toString(), choice->property("currentValueText").toString());
        QVERIFY(!control->property("currentText").toString().isEmpty());
        // The appended custom option is informational; choosing it never writes an invalid preset.
        QVERIFY(QMetaObject::invokeMethod(control, "activated", Q_ARG(int, control->property("currentIndex").toInt())));
    }
    QCOMPARE(item(view, "previewPointerSize")->property("currentValueText").toString(),
             QLocale().toString(1.25, 'g', 6) + QString::fromUtf8("×"));
    QCOMPARE(backend.cursorScale(), 1.25);
    QCOMPARE(backend.cursorIdleHide(), 1250);
    QCOMPARE(backend.microphoneGainDb(), 2.5);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewPointerSize")->findChild<QObject*>("editorChoiceControl"),
                                    "activated", Q_ARG(int, 1)));
    QCOMPARE(backend.cursorScale(), 1.5);
    QCOMPARE(backend.mp4SourceFrameRate(), 30);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openExport"));
    QObject* popup = view->rootObject()->findChild<QObject*>("previewExportPanel");
    QTRY_VERIFY(popup && popup->property("opened").toBool());
    QObject* fps = delegate(view, "previewMp4FrameRate");
    QVERIFY(fps);
    QCOMPARE(fps->property("choices").value<QJSValue>().toVariant().toStringList(), QStringList({"Original", "15 fps"}));
    backend.setMp4FrameRate(30);
    QCOMPARE(fps->property("currentIndex").toInt(), 0);
    QCOMPARE(backend.mp4OutputFrameRate(), 30);
    QVERIFY(QMetaObject::invokeMethod(fps, "activated", Q_ARG(int, 1)));
    QCOMPARE(backend.mp4FrameRate(), 15);
    QCOMPARE(backend.mp4OutputFrameRate(), 15);
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(!popup->property("opened").toBool());
    backend.requestDiscard();
}

void tst_RecordingEditor::switchFollowsUndo()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSection", Q_ARG(QVariant, "appearance")));
    QQuickItem* toggle = item(view, "previewAppearanceToggle");
    QVERIFY(toggle);
    QTRY_VERIFY(toggle->isVisible());
    const bool before = backend.appearanceEnabled();
    QCOMPARE(toggle->property("checked").toBool(), before);
    // A real click: the switch must keep following the backend afterwards.
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      toggle->mapToScene(QPointF(toggle->width() - 10, toggle->height() / 2)).toPoint());
    QTRY_COMPARE(backend.appearanceEnabled(), !before);
    QCOMPARE(toggle->property("checked").toBool(), !before);
    backend.undo();
    QCOMPARE(backend.appearanceEnabled(), before);
    QTRY_COMPARE(toggle->property("checked").toBool(), before);
    backend.requestDiscard();
}

void tst_RecordingEditor::deleteCutsSelectedClip()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(seek(view, 200));
    QVERIFY(backend.splitAt(1000));
    // The playhead is in part 0; part 1 is selected without seeking.
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "selectPart", Q_ARG(QVariant, 1)));
    view->rootObject()->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Delete);
    QVERIFY(!backend.timelineEdit().parts[0].deleted);
    QVERIFY(backend.timelineEdit().parts[1].deleted);
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), 1);
    backend.requestDiscard();
}

void tst_RecordingEditor::cutButtonFollowsSelectedClip()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(backend.splitAt(1000));
    // Select clip B, then seek into clip A: the toolbar still acts on B, like Delete.
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "selectPart", Q_ARG(QVariant, 1)));
    QVERIFY(seek(view, 200));
    QCOMPARE(view->rootObject()->property("playheadPart").toInt(), 0);
    QQuickItem* cut = item(view, "previewPartCut");
    QVERIFY(cut);
    QVERIFY(cut->property("tooltipText").toString().contains("selected"));
    QVERIFY(QMetaObject::invokeMethod(cut, "clicked"));
    QVERIFY(!backend.timelineEdit().parts[0].deleted);
    QVERIFY(backend.timelineEdit().parts[1].deleted);
    QCOMPARE(view->rootObject()->property("selectedPart").toInt(), 1);
    QCOMPARE(cut->property("text").toString(), QStringLiteral("Keep Part"));
    QVERIFY(cut->property("selected").toBool());
    // Clicking again puts the selected clip back.
    QVERIFY(QMetaObject::invokeMethod(cut, "clicked"));
    QVERIFY(!backend.timelineEdit().parts[1].deleted);
    // Without a selection it falls back to the clip at the playhead.
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSection", Q_ARG(QVariant, "")));
    QVERIFY(QMetaObject::invokeMethod(cut, "clicked"));
    QVERIFY(backend.timelineEdit().parts[0].deleted);
    QVERIFY(!backend.timelineEdit().parts[1].deleted);
    backend.requestDiscard();
}

void tst_RecordingEditor::adjacentShortEffectsStaySeparate()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    // Two short masks that touch in time but would cover each other at their minimum width.
    QVERIFY(backend.addMask() >= 0);
    QVERIFY(backend.addMask() >= 0);
    QVERIFY(backend.setMaskRange(0, 500, 501));
    QVERIFY(backend.setMaskRange(1, 501, 502));
    backend.setSelectedMask(-1);
    QQuickItem* first = nullptr;
    QQuickItem* second = nullptr;
    QTRY_VERIFY((first = delegate(view, "previewMaskSegment0")) && (second = delegate(view, "previewMaskSegment1")));
    QTRY_VERIFY(first->y() != second->y());
    const QRectF a = first->mapRectToScene(QRectF(QPointF(), first->size()));
    const QRectF b = second->mapRectToScene(QRectF(QPointF(), second->size()));
    QVERIFY(!a.intersects(b));
    // Dragging the first one's centre moves the first one, not the second.
    QSignalSpy rendered(view, &QQuickWindow::afterAnimating);
    view->requestUpdate();
    QVERIFY(rendered.wait(2000));
    const QPoint from = a.center().toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view, from + QPoint(60, 0), 30);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, from + QPoint(60, 0));
    QVERIFY(backend.masks().at(0).toMap().value("start").toLongLong() > 500);
    QCOMPARE(backend.masks().at(1).toMap().value("start").toLongLong(), 501);
    backend.requestDiscard();
}

void tst_RecordingEditor::narrowHeaderKeepsActionsVisible()
{
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("a-very-long-recording-file-name-for-layout-stress-2026-11-04.mp4"));
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(640, 480);
    QTRY_VERIFY(!view->rootObject()->property("wideEditor").toBool());
    // A long translation of the status must give way to the actions.
    view->rootObject()->setProperty("projectStatusText",
        QStringLiteral("Nicht gespeicherte Bildschirmaufnahme ohne bearbeitbares Projekt"));
    QSignalSpy rendered(view, &QQuickWindow::afterAnimating);
    view->requestUpdate();
    QVERIFY(rendered.wait(2000));
    for (const char* name : {"previewUndoButton", "previewRedoButton", "previewInspectorToggle",
                             "previewExportOptions", "previewDiscardButton"}) {
        QQuickItem* button = item(view, name);
        QVERIFY2(button, name);
        const QRectF r = button->mapRectToScene(QRectF(QPointF(), button->size()));
        QVERIFY2(r.left() >= 0 && r.right() <= view->width() && r.width() >= 24,
                 qPrintable(QStringLiteral("%1 at %2..%3").arg(name).arg(r.left()).arg(r.right())));
    }
    QQuickItem* chip = item(view, "previewStatusChip");
    QVERIFY(chip);
    QVERIFY(chip->width() <= 120);
    backend.requestDiscard();
}

void tst_RecordingEditor::dialogButtonPreservesMouseFocus()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQmlComponent component(view->engine());
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls.Basic
        import SnapTrayQml
        Item {
            width: 300; height: 150; z: 100
            TextField { objectName: "focusField"; width: 250; text: "pending input" }
            DialogButton { objectName: "focusButton"; y: 60; text: "Action" }
        }
    )", QUrl("qrc:/SnapTrayQml/FocusFixture.qml"));
    std::unique_ptr<QObject> fixture(component.create());
    QVERIFY2(fixture, qPrintable(component.errorString()));
    auto* container = qobject_cast<QQuickItem*>(fixture.get());
    QVERIFY(container);
    container->setParentItem(view->rootObject());
    auto* field = container->findChild<QQuickItem*>("focusField");
    auto* button = container->findChild<QQuickItem*>("focusButton");
    QVERIFY(field && button);
    field->forceActiveFocus();
    QTRY_VERIFY(field->hasActiveFocus());
    QSignalSpy editingFinished(field, SIGNAL(editingFinished()));
    QSignalSpy clicked(button, SIGNAL(clicked()));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(30, 20)).toPoint());
    QCOMPARE(clicked.size(), 1);
    QVERIFY(field->hasActiveFocus());
    QCOMPARE(editingFinished.size(), 0);
    button->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(view, Qt::Key_Space);
    QCOMPARE(clicked.size(), 2);
    fixture.reset();
    backend.requestDiscard();
}

QTEST_MAIN(tst_RecordingEditor)
#include "tst_RecordingEditor.moc"
