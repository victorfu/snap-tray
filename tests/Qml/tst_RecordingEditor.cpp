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
#include <QAccessible>
#include <QPainter>
#include <QPainterPath>
#include <QFile>
#include <QFileInfo>
#include <QTranslator>
#include "ToolbarStyle.h"
#include "ui/DesignSystem.h"
#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"
#include "recording/AudioTracks.h"
#include "recording/PreviewEditHistory.h"
#include "recording/RecordingProject.h"

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
    void closeWhileExportingShowsConfirmation();
    void dialogButtonIgnoresKeyRepeat();
    void addEffectAppliesCropFirst();
    void exportSummaryShowsSavedSize();
    void startOnlyEditKeepsOpenEnd();
    void switchKeepsToggleAccessibility();
    void escapeCancelsTimelineDrags();
    void escapeLeavesCategory();
    void categoryFollowsFormat();
    void timeFieldsKeepTypedKeys();
    void redoAndReopenKeepSelectionHonest();
    void inspectorControlsAreNamed();
    void outputTimeFollowsCutsAndSpeed();
    void canvasClickSelectsObjects();
    void backgroundImageShowsThumbnail();
    void splitMarkerKeepsKeyboard();
    void designEvidence_data();
    void designEvidence();
    void designEvidenceError_data();
    void designEvidenceError();
    void operationVideo_data();
    void operationVideo();
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

// Frames of a scripted session for the operation videos (#218-#220): real
// mouse and key events, with the pointer and a caption drawn in (a window
// grab has neither).
class FrameRecorder
{
public:
    FrameRecorder(QQuickView* view, const QString& dir) : m_view(view), m_dir(dir) { QDir().mkpath(dir); }
    void caption(const QString& text) { m_caption = text; }
    void frame(int hold = 1)
    {
        for (int i = 0; i < 2; ++i) {
            QSignalSpy rendered(m_view, &QQuickWindow::afterAnimating);
            m_view->requestUpdate();
            rendered.wait(200);
        }
        QImage image = m_view->grabWindow();
        const qreal dpr = image.devicePixelRatio();
        image = image.scaledToWidth(qRound(image.width() / dpr), Qt::SmoothTransformation);
        image.setDevicePixelRatio(1.0);
        QImage out(image.width(), image.height() + kCaption, QImage::Format_RGB32);
        out.fill(QColor("#101114"));
        QPainter painter(&out);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawImage(0, 0, image);
        if (m_cursor.x() >= 0) {
            QPainterPath arrow;
            const QPointF p = m_cursor;
            arrow.moveTo(p);
            arrow.lineTo(p + QPointF(0, 17));
            arrow.lineTo(p + QPointF(4.5, 13));
            arrow.lineTo(p + QPointF(8, 20));
            arrow.lineTo(p + QPointF(10.5, 19));
            arrow.lineTo(p + QPointF(7, 12));
            arrow.lineTo(p + QPointF(12.5, 12));
            arrow.closeSubpath();
            if (m_pressed) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(255, 196, 0, 110));
                painter.drawEllipse(p, 14, 14);
            }
            painter.setPen(QPen(Qt::white, 1.5));
            painter.setBrush(Qt::black);
            painter.drawPath(arrow);
        }
        QFont font = painter.font();
        font.setPixelSize(18);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#f2f2f2"));
        painter.drawText(QRect(16, image.height(), image.width() - 32, kCaption), Qt::AlignVCenter, m_caption);
        painter.end();
        for (int i = 0; i < hold; ++i) {
            out.save(QStringLiteral("%1/frame-%2.png").arg(m_dir).arg(m_count++, 5, 10, QLatin1Char('0')));
        }
    }
    void moveTo(const QPoint& to, int steps = 8)
    {
        const QPoint from = m_cursor.x() < 0 ? QPoint(m_view->width() / 2, m_view->height() / 2) : m_cursor;
        for (int i = 1; i <= steps; ++i) {
            m_cursor = from + (to - from) * i / steps;
            QTest::mouseMove(m_view, m_cursor);
            frame();
        }
    }
    void click(const QPoint& at, int hold = 6)
    {
        moveTo(at);
        m_pressed = true;
        QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, at);
        frame(2);
        QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, at);
        m_pressed = false;
        frame(hold);
    }
    void doubleClick(const QPoint& at, int hold = 6)
    {
        moveTo(at);
        m_pressed = true;
        QTest::mouseDClick(m_view, Qt::LeftButton, Qt::NoModifier, at);
        frame(2);
        m_pressed = false;
        frame(hold);
    }
    // A drag; `escape` presses Escape before letting go.
    void drag(const QPoint& from, const QPoint& to, bool escape = false, int steps = 12)
    {
        moveTo(from);
        m_pressed = true;
        QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, from);
        frame(2);
        for (int i = 1; i <= steps; ++i) {
            m_cursor = from + (to - from) * i / steps;
            QTest::mouseMove(m_view, m_cursor);
            frame();
        }
        if (escape) {
            QTest::keyClick(m_view, Qt::Key_Escape);
            frame(6);
        }
        QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, to);
        m_pressed = false;
        frame(8);
    }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, int hold = 6)
    {
        QTest::keyClick(m_view, key, modifiers);
        frame(hold);
    }
    void type(const QString& text)
    {
        for (const QChar c : text) {
            QTest::keyClick(m_view, c.toLatin1());
            frame(2);
        }
    }
    QPoint centerOf(QQuickItem* item) const { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }

private:
    static constexpr int kCaption = 44;
    QQuickView* m_view;
    QString m_dir;
    QString m_caption;
    QPoint m_cursor{-1, -1};
    bool m_pressed = false;
    int m_count = 0;
};
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
    // The window was just resized (asynchronously on X11): take positions only
    // once both segments have stopped moving between frames.
    QRectF a;
    QRectF b;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        const QRectF nowA = first->mapRectToScene(QRectF(QPointF(), first->size()));
        const QRectF nowB = second->mapRectToScene(QRectF(QPointF(), second->size()));
        QSignalSpy frame(view, &QQuickWindow::afterAnimating);
        view->requestUpdate();
        frame.wait(500);
        const bool stable = nowA == a && nowB == b
            && nowA == first->mapRectToScene(QRectF(QPointF(), first->size()))
            && nowB == second->mapRectToScene(QRectF(QPointF(), second->size()));
        a = nowA;
        b = nowB;
        return stable;
    }(), 5000);
    QVERIFY(!a.intersects(b));
    // Dragging the first one's centre moves the first one, not the second.
    const QPoint from = a.center().toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(view, from + QPoint(60, 0), 30);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, from + QPoint(60, 0));
    QTRY_VERIFY(backend.masks().at(0).toMap().value("start").toLongLong() > 500);
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

void tst_RecordingEditor::closeWhileExportingShowsConfirmation()
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
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openExport"));
    QObject* popup = view->rootObject()->findChild<QObject*>("previewExportPanel");
    QTRY_VERIFY(popup && popup->property("opened").toBool());
    // The native close asks for confirmation; the export popup must not hide it.
    view->close();
    if (!backend.discardConfirmationPending()) backend.requestDiscard();
    QTRY_VERIFY(backend.discardConfirmationPending());
    QTRY_VERIFY(!popup->property("opened").toBool());
    QQuickItem* confirmation = item(view, "discardConfirmation");
    QVERIFY(confirmation && confirmation->isVisible());
    // The keys go to the visible confirmation: Enter keeps the recording (the default).
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_VERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);
    // And the mouse reaches its buttons.
    backend.requestDiscard();
    QTRY_VERIFY(confirmation->isVisible());
    QQuickItem* keep = item(view, "discardKeepButton");
    QVERIFY(keep);
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      keep->mapToScene(QPointF(keep->width() / 2, keep->height() / 2)).toPoint());
    QTRY_VERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);
    backend.requestDiscard();
}

void tst_RecordingEditor::dialogButtonIgnoresKeyRepeat()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openExport"));
    QQuickItem* back = item(view, "previewExportBack");
    QVERIFY(back);
    QTRY_VERIFY(back->isVisible());
    QSignalSpy clicked(back, SIGNAL(clicked()));
    const auto send = [&](QEvent::Type type, int key, bool repeat) {
        QKeyEvent event(type, key, Qt::NoModifier, QString(), repeat);
        QCoreApplication::sendEvent(view, &event);
    };
    // Space: one press held down (with auto-repeat) activates once, on release.
    back->forceActiveFocus();
    QVERIFY(back->hasActiveFocus());
    send(QEvent::KeyPress, Qt::Key_Space, false);
    send(QEvent::KeyPress, Qt::Key_Space, true);
    send(QEvent::KeyPress, Qt::Key_Space, true);
    QCOMPARE(clicked.count(), 0);
    send(QEvent::KeyRelease, Qt::Key_Space, false);
    QCOMPARE(clicked.count(), 1);
    // Return: once per press, never on auto-repeat.
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openExport"));
    QTRY_VERIFY(back->isVisible());
    back->forceActiveFocus();
    send(QEvent::KeyPress, Qt::Key_Return, false);
    send(QEvent::KeyPress, Qt::Key_Return, true);
    send(QEvent::KeyRelease, Qt::Key_Return, false);
    QCOMPARE(clicked.count(), 2);
    backend.requestDiscard();
}

void tst_RecordingEditor::addEffectAppliesCropFirst()
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
    QTRY_VERIFY(item(view, "previewCropButton")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewCropButton"), "clicked"));
    QQuickItem* crop = item(view, "previewCropOverlay");
    QTRY_VERIFY(crop && crop->property("editing").toBool());
    // An unreadable size typed into the crop panel blocks every Add effect entry.
    QQuickItem* field = nullptr;
    QTRY_VERIFY((field = delegate(view, "previewCrop_width")) != nullptr);
    const QString width = field->property("text").toString();
    field->setProperty("text", QString());
    field->setProperty("edited", true);
    const int masks = backend.masks().size();
    for (const char* name : {"previewAddMask", "previewAddText", "previewAddZoom"}) {
        QQuickItem* entry = item(view, name);
        QVERIFY2(entry, name);
        QVERIFY(QMetaObject::invokeMethod(entry, "clicked"));
    }
    QCOMPARE(backend.masks().size(), masks);
    QCOMPARE(backend.overlays().size(), 0);
    QVERIFY(crop->property("editing").toBool());
    // With valid input the draft is applied, then the effect is added and shown.
    field->setProperty("text", width);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAddMask"), "clicked"));
    QTRY_VERIFY(!crop->property("editing").toBool());
    QCOMPARE(backend.masks().size(), masks + 1);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    backend.requestDiscard();
}

void tst_RecordingEditor::exportSummaryShowsSavedSize()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY(backend.duration() > 0);
    QQuickItem* summary = item(view, "previewOutputSummary");
    QVERIFY(summary);
    const auto expected = [&]() {
        const QSize size = backend.exportSize();
        return QStringLiteral("Output %1 × %2").arg(size.width()).arg(size.height());
    };
    QTRY_VERIFY(summary->property("text").toString().contains(expected()));
    if (backend.mp4OutputOptionsAvailable()) {
        backend.setMp4Resolution(3);
        backend.setMp4CustomWidth(80);
        QTRY_COMPARE(backend.exportSize().width(), 80);
        QTRY_VERIFY2(summary->property("text").toString().contains(expected()),
                     qPrintable(summary->property("text").toString()));
        backend.setMp4Resolution(2); // 720p: never larger than the recording
        QTRY_VERIFY(summary->property("text").toString().contains(expected()));
    }
    // A frame adds padding around the picture: the summary follows the saved size.
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.setAppearanceEnabled(true);
    backend.setAppearancePadding(3);
    QTRY_VERIFY2(summary->property("text").toString().contains(expected()),
                 qPrintable(summary->property("text").toString() + " vs " + expected()));
    // The estimate is shown once, as the backend words it.
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QObject* popup = view->rootObject()->findChild<QObject*>("previewExportPanel");
    QVERIFY(popup);
    QTRY_VERIFY(!backend.mp4EstimateText().isEmpty());
    QCOMPARE(popup->property("footerText").toString(), backend.mp4EstimateText());
    QVERIFY(!popup->property("footerText").toString().contains("About About"));
    backend.requestDiscard();
}

void tst_RecordingEditor::startOnlyEditKeepsOpenEnd()
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
    for (const QString kind : {QStringLiteral("Mask"), QStringLiteral("Overlay")}) {
        const bool mask = kind == QStringLiteral("Mask");
        QVERIFY(mask ? backend.setMaskRange(0, 200, -1) : backend.setOverlayRange(0, 200, -1));
        if (mask) backend.setSelectedMask(0); else backend.setSelectedOverlay(0);
        QQuickItem* start = item(view, qPrintable("preview" + kind + "StartField"));
        QVERIFY(start);
        QTRY_VERIFY(start->isVisible());
        // Only the start is typed (not applied with Enter), then the selection changes.
        start->setProperty("text", backend.formatPreciseTime(300));
        start->setProperty("edited", true);
        QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSection", Q_ARG(QVariant, "")));
        const QVariantMap range = (mask ? backend.masks() : backend.overlays()).at(0).toMap();
        QCOMPARE(range.value("start").toLongLong(), 300);
        QCOMPARE(range.value("end").toLongLong(), -1);
    }
    backend.requestDiscard();
}

void tst_RecordingEditor::switchKeepsToggleAccessibility()
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
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(toggle);
    QVERIFY(accessible);
    QVERIFY(accessible->state().checkable);
    QCOMPARE(bool(accessible->state().checked), backend.appearanceEnabled());
    QVERIFY(accessible->actionInterface());
    QVERIFY(accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    const bool before = backend.appearanceEnabled();
    accessible->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QTRY_COMPARE(backend.appearanceEnabled(), !before);
    QCOMPARE(toggle->property("checked").toBool(), !before);
    QCOMPARE(bool(accessible->state().checked), !before);
    // Undo still drives it.
    backend.undo();
    QTRY_COMPARE(toggle->property("checked").toBool(), before);
    backend.requestDiscard();
}

// #219: Escape during a split or trim drag drops the drag: nothing changes,
// no undo step, and the editor does not ask to close.
void tst_RecordingEditor::escapeCancelsTimelineDrags()
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
    QVERIFY(backend.splitAt(1000));
    const QVariantList splits = backend.timelineSplits();
    QQuickItem* marker = nullptr;
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    QTRY_VERIFY(marker->isVisible() && marker->height() > 0);
    const QPoint center = marker->mapToScene(QPointF(marker->width() / 2, marker->height() / 2)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(view, center + QPoint(60, 0), 30);
    QTRY_VERIFY(view->rootObject()->property("timelineDragActive").toBool());
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 0));
    QCOMPARE(backend.timelineSplits(), splits);
    QVERIFY(!backend.discardConfirmationPending());

    // A trim handle: the live change is undone.
    backend.setTrimStart(300);
    backend.commitTrimEdit();
    QQuickItem* timeline = item(view, "previewTimeline");
    QQuickItem* scrubber = item(view, "previewScrubber");
    QVERIFY(timeline && scrubber);
    QVariant handleX;
    QVERIFY(QMetaObject::invokeMethod(timeline, "posToX", Q_RETURN_ARG(QVariant, handleX), Q_ARG(QVariant, 300)));
    const QPoint handle = scrubber->mapToScene(
        QPointF(handleX.toReal(), scrubber->property("laneCenterY").toReal())).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
    QTest::mouseMove(view, handle + QPoint(80, 0), 30);
    QTRY_VERIFY(backend.trimStart() > 300);
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(backend.trimStart(), qint64(300));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, handle + QPoint(80, 0));
    QCOMPARE(backend.trimStart(), qint64(300));
    QVERIFY(!backend.discardConfirmationPending());
    // The drag left no step of its own: one undo goes back past the trim.
    backend.undo();
    QCOMPARE(backend.trimStart(), qint64(0));

    // A focused time field takes Escape for itself, but not from a drag.
    backend.setTrimStart(300);
    backend.commitTrimEdit();
    QQuickItem* field = item(view, "previewTrimStartField");
    QVERIFY(field);
    QTRY_VERIFY(field->isVisible());
    field->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(field->hasActiveFocus());
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
    QTest::mouseMove(view, handle + QPoint(80, 0), 30);
    QTRY_VERIFY(backend.trimStart() > 300);
    QTest::keyClick(view, Qt::Key_Escape);
    QCOMPARE(backend.trimStart(), qint64(300));
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, handle + QPoint(80, 0));
    QCOMPARE(backend.trimStart(), qint64(300));
    // A split drag too.
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    QVERIFY(field->hasActiveFocus());
    const QPoint split = marker->mapToScene(QPointF(marker->width() / 2, marker->height() / 2)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, split);
    QTest::mouseMove(view, split + QPoint(60, 0), 30);
    QTRY_VERIFY(view->rootObject()->property("timelineDragActive").toBool());
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, split + QPoint(60, 0));
    QCOMPARE(backend.timelineSplits(), splits);
    QVERIFY(!backend.discardConfirmationPending());

    // While a crop is edited, Escape cancels the drag and keeps the draft.
    view->rootObject()->forceActiveFocus();
    QTRY_VERIFY(view->rootObject()->hasActiveFocus());
    // Cropping needs a frame on screen.
    QVERIFY(seek(view, 500));
    QQuickItem* player = item(view, "previewVideoPlayer");
    QVERIFY(player);
    QTRY_VERIFY(player->property("contentRect").toRectF().width() > 0);
    QTRY_VERIFY(item(view, "previewCropButton")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewCropButton"), "clicked"));
    QQuickItem* crop = item(view, "previewCropOverlay");
    QTRY_VERIFY(crop && crop->property("editing").toBool());
    view->rootObject()->forceActiveFocus();
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    const QPoint cropSplit = marker->mapToScene(QPointF(marker->width() / 2, marker->height() / 2)).toPoint();
    QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, cropSplit);
    QTest::mouseMove(view, cropSplit + QPoint(60, 0), 30);
    QTRY_VERIFY(view->rootObject()->property("timelineDragActive").toBool());
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, cropSplit + QPoint(60, 0));
    QCOMPARE(backend.timelineSplits(), splits);
    QVERIFY(crop->property("editing").toBool());
    QVERIFY(!backend.discardConfirmationPending());
    backend.requestDiscard();
}

// #218: Escape in a whole-recording category returns to Project settings
// instead of asking to close the editor.
void tst_RecordingEditor::escapeLeavesCategory()
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
    QObject* root = view->rootObject();
    QTRY_VERIFY(root->property("wideEditor").toBool());
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "appearance")));
    QTRY_COMPARE(root->property("inspectorKind").toString(), QStringLiteral("appearance"));
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_COMPARE(root->property("inspectorKind").toString(), QStringLiteral("project"));
    QVERIFY(!backend.discardConfirmationPending());
    QVERIFY(view->isVisible());
    backend.requestDiscard();
}

// #217: a category the new format cannot use is not left in the title, and
// the reason names the real cause.
void tst_RecordingEditor::categoryFollowsFormat()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    for (const auto track : {SnapTray::AudioTracks::Track::Microphone, SnapTray::AudioTracks::Track::SystemAudio}) {
        SnapTray::AudioTrackWriter writer(SnapTray::AudioTracks::pathFor(video, track));
        QVERIFY(writer.open());
    }
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QVERIFY(backend.hasSeparateTracks());
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QObject* root = view->rootObject();
    QQuickItem* audio = item(view, "previewAudioOptions");
    QVERIFY(audio);
    QTRY_VERIFY(audio->isVisible());
    QVERIFY(!audio->isEnabled());
    QVERIFY2(audio->property("unavailableReason").toString().contains(QStringLiteral("no sound")),
             qPrintable(audio->property("unavailableReason").toString()));

    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "appearance")));
    QTRY_COMPARE(root->property("inspectorKind").toString(), QStringLiteral("appearance"));
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    const QString expected = backend.appearanceAvailable() ? QStringLiteral("appearance") : QStringLiteral("project");
    QTRY_COMPARE(root->property("inspectorKind").toString(), expected);
    QCOMPARE(item(view, "previewAppearancePanel")->isVisible(), backend.appearanceAvailable());
    if (!backend.audioMixAvailable()) {
        QVERIFY2(audio->property("unavailableReason").toString().contains(QStringLiteral("MP4")),
                 qPrintable(audio->property("unavailableReason").toString()));
    }
    // A whole editor disabled (as during an export) keeps the summaries: no
    // category claims a missing capability it has.
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    QQuickItem* canvas = item(view, "previewAppearanceOptions");
    QQuickItem* inspector = item(view, "editorInspector");
    QTRY_VERIFY(canvas->isEnabled());
    inspector->setEnabled(false);
    QVERIFY(!canvas->isEnabled());
    QVERIFY(canvas->property("available").toBool());
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(canvas);
    QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Description), canvas->property("summary").toString());
    inspector->setEnabled(true);
    backend.requestDiscard();
}

// #218: typing in a time field never plays, splits, cuts or seeks.
void tst_RecordingEditor::timeFieldsKeepTypedKeys()
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
    QVERIFY(seek(view, 800));
    const int mask = backend.addMask();
    QVERIFY(mask >= 0);
    QVERIFY(backend.setMaskRange(mask, 200, 1500)); // a range shows the time fields
    backend.setSelectedMask(mask);
    QQuickItem* field = item(view, "previewMaskStartField");
    QQuickItem* player = item(view, "previewVideoPlayer");
    QVERIFY(field && player);
    QTRY_VERIFY(field->isVisible());
    field->forceActiveFocus();
    QTRY_VERIFY(field->hasActiveFocus());
    QMetaObject::invokeMethod(field, "selectAll");
    QTest::keyClick(view, Qt::Key_Home);
    for (const Qt::Key key : {Qt::Key_Space, Qt::Key_S, Qt::Key_Left, Qt::Key_Right, Qt::Key_Delete,
                              Qt::Key_Backspace, Qt::Key_Period, Qt::Key_M}) {
        QTest::keyClick(view, key);
    }
    QVERIFY(!player->property("playing").toBool());
    QVERIFY(qAbs(player->property("position").toLongLong() - 800) <= 100);
    QVERIFY(backend.timelineSplits().isEmpty());
    QCOMPARE(backend.masks().size(), 1);
    QCOMPARE(backend.selectedMask(), mask);
    QVERIFY(!player->property("muted").toBool());
    QVERIFY(field->hasActiveFocus());
    backend.requestDiscard();
}

// #218: undo/redo and reopening a project never leave the inspector on an
// object that changed under it.
void tst_RecordingEditor::redoAndReopenKeepSelectionHonest()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    {
        RecordingPreviewBackend backend(video);
        backend.setSelectedFormat(RecordingPreviewBackend::GIF);
        QQuickView* view = openPreview(backend);
        QVERIFY(view);
        view->resize(1200, 800);
        QTRY_VERIFY(backend.duration() > 0);
        QObject* root = view->rootObject();
        QVERIFY(backend.addMask() >= 0);
        QVERIFY(backend.setMaskRange(0, 0, 900));
        QVERIFY(backend.addMask() >= 0);
        QVERIFY(backend.setMaskRange(1, 1000, 1800));
        backend.setSelectedMask(1);
        QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
        QTest::keyClick(view, Qt::Key_Delete);
        QTRY_COMPARE(backend.masks().size(), 1);
        QCOMPARE(backend.selectedMask(), -1);
        QVERIFY(QMetaObject::invokeMethod(root, "undoEdit"));
        QTRY_COMPARE(backend.masks().size(), 2);
        QCOMPARE(backend.selectedMask(), -1);
        backend.setSelectedMask(0);
        QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
        QVERIFY(QMetaObject::invokeMethod(root, "redoEdit"));
        QTRY_COMPARE(backend.masks().size(), 1);
        QCOMPARE(backend.selectedMask(), -1);
        QTRY_COMPARE(root->property("inspectorKind").toString(), QStringLiteral("project"));
        backend.requestDiscard();
    }

    // A reopened project starts on Project settings; its masks select normally.
    SnapTray::PreviewEditState state;
    SnapTray::RedactionMask first;
    first.rect = QRect(10, 10, 40, 30);
    first.startMs = 0;
    first.endMs = 900;
    SnapTray::RedactionMask second = first;
    second.startMs = 1000;
    second.endMs = 1800;
    second.style = SnapTray::RedactionMask::Style::Blur;
    state.redaction.masks = {first, second};
    SnapTray::RecordingProject project;
    project.dir = dir.path();
    project.videoName = QStringLiteral("recording.mp4");
    project.recipe = SnapTray::RecordingRecipe::toJson(state, {});
    RecordingPreviewBackend reopened(project.videoPath(), true);
    reopened.setSelectedFormat(RecordingPreviewBackend::GIF);
    reopened.setProject(project);
    QQuickView* view = openPreview(reopened);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY_WITH_TIMEOUT(!reopened.projectLoading(), 10000);
    QTRY_COMPARE(reopened.masks().size(), 2);
    QCOMPARE(reopened.selectedMask(), -1);
    QObject* root = view->rootObject();
    QCOMPARE(root->property("inspectorKind").toString(), QStringLiteral("project"));
    reopened.setSelectedMask(1);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    QTRY_COMPARE(item(view, "previewMaskStartField")->property("text").toString(),
                 reopened.formatPreciseTime(1000));
    reopened.requestDiscard();
}

// #217/#218: every keyboard-reachable control in each inspector has a
// spoken name.
void tst_RecordingEditor::inspectorControlsAreNamed()
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
    QObject* root = view->rootObject();
    QQuickItem* inspector = item(view, "editorInspector");
    QVERIFY(inspector);
    QStringList unnamed;
    const auto sweep = [&](const QString& state) {
        QTest::qWait(100);
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* node) {
            if (!node->isVisible()) return;
            if (node->activeFocusOnTab() && node->isEnabled()) {
                QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(node);
                const QString name = accessible ? accessible->text(QAccessible::Name).trimmed() : QString();
                if (name.isEmpty()) {
                    unnamed << QStringLiteral("%1: %2 (%3)").arg(state, node->objectName(),
                                                                QString::fromLatin1(node->metaObject()->className()));
                }
            }
            for (QQuickItem* child : node->childItems()) walk(child);
        };
        walk(inspector);
    };
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    sweep(QStringLiteral("project"));
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "appearance")));
    QTRY_VERIFY(item(view, "previewAppearancePanel")->isVisible());
    sweep(QStringLiteral("appearance"));
    QVERIFY(backend.splitAt(1000));
    QVERIFY(QMetaObject::invokeMethod(root, "selectPart", Q_ARG(QVariant, 1)));
    QTRY_VERIFY(item(view, "previewClipPanel")->isVisible());
    sweep(QStringLiteral("clip"));
    const int mask = backend.addMask();
    backend.setSelectedMask(mask);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    sweep(QStringLiteral("mask"));
    const int overlay = backend.addTextOverlay(0);
    QVERIFY(overlay >= 0);
    backend.setSelectedOverlay(overlay);
    QTRY_VERIFY(item(view, "previewOverlayPanel")->isVisible());
    sweep(QStringLiteral("text"));
    if (backend.addZoomAt(1500) >= 0) {
        backend.setSelectedZoom(0);
        QTRY_VERIFY(item(view, "previewZoomPanel")->isVisible());
        sweep(QStringLiteral("zoom"));
    }
    QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QLatin1String("\n"))));
    backend.requestDiscard();
}

// #219: after cutting the middle and speeding up the end, the editor shows
// where the playhead lands in the export, and an effect that lies only in
// the cut part is marked as not exported.
void tst_RecordingEditor::outputTimeFollowsCutsAndSpeed()
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
    const qint64 duration = backend.duration();
    QQuickItem* output = item(view, "previewOutputTime");
    QVERIFY(output);
    QVERIFY(!output->isVisible()); // nothing edited: source and export agree
    QVERIFY(backend.splitAt(600));
    QVERIFY(backend.splitAt(1400));
    backend.setPartDeleted(1, true);
    backend.setPartSpeed(2, 2.0);
    QTRY_VERIFY(output->isVisible());
    const qint64 expectedLength = 600 + (duration - 1400) / 2;
    QVERIFY(qAbs(backend.outputDuration() - expectedLength) <= 1);

    QVERIFY(seek(view, 1000)); // inside the cut part
    QTRY_VERIFY2(output->property("text").toString().startsWith(QStringLiteral("Not exported")),
                 qPrintable(output->property("text").toString()));
    QVERIFY(seek(view, 1600));
    const qint64 at = backend.outputTimeAt(1600);
    QVERIFY(qAbs(at - 700) <= 1); // 600 kept + 200 ms at 2×
    QTRY_VERIFY2(output->property("text").toString().contains(backend.formatPreciseTime(at)),
                 qPrintable(output->property("text").toString()));

    const int cutOnly = backend.addMask();
    QVERIFY(backend.setMaskRange(cutOnly, 700, 1300));
    const int kept = backend.addMask();
    QVERIFY(backend.setMaskRange(kept, 100, 500));
    QQuickItem* hidden = nullptr;
    QQuickItem* shown = nullptr;
    QTRY_VERIFY((hidden = delegate(view, QStringLiteral("previewMaskSegment%1").arg(cutOnly))) != nullptr);
    QTRY_VERIFY((shown = delegate(view, QStringLiteral("previewMaskSegment%1").arg(kept))) != nullptr);
    QTRY_VERIFY(!hidden->property("exported").toBool());
    QVERIFY(shown->property("exported").toBool());
    // Putting the part back brings the effect back.
    backend.setPartDeleted(1, false);
    QTRY_VERIFY(hidden->property("exported").toBool());
    backend.requestDiscard();
}

// #218: clicking a text or mask on the picture selects it, like its block
// on the timeline; a click on nothing still plays.
void tst_RecordingEditor::canvasClickSelectsObjects()
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
    QObject* root = view->rootObject();
    QQuickItem* area = item(view, "previewVideoClickArea");
    QQuickItem* player = item(view, "previewVideoPlayer");
    QVERIFY(area && player);
    const int overlay = backend.addTextOverlay(0);
    QVERIFY(overlay >= 0);
    backend.setOverlayText(overlay, QStringLiteral("Click me"));
    QVERIFY(backend.setOverlayRange(overlay, 0, 600));
    const int mask = backend.addMask();
    QVERIFY(mask >= 0);
    QVERIFY(backend.setMaskRange(mask, 0, 1800));
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    // Hit-testing follows the shown frame, as the objects are drawn.
    const auto showFrame = [&](qint64 ms) {
        return seek(view, ms) && QTest::qWaitFor([player, ms] {
            return qAbs(player->property("framePosition").toLongLong() - ms) <= 100;
        }, 5000);
    };
    QVERIFY(showFrame(300));
    QTRY_VERIFY(player->property("contentRect").toRectF().width() > 0);
    const auto centerOf = [&](const QRectF& r) { return area->mapToScene(r.center()).toPoint(); };

    // A split marker has the keyboard; the selection takes it, so Delete
    // would act on the object and not join the clips.
    QVERIFY(backend.splitAt(1200));
    QQuickItem* marker = nullptr;
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    marker->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(marker->hasActiveFocus());
    QRectF overlayRect;
    QVERIFY(QMetaObject::invokeMethod(&backend, "overlayRectInView", Q_RETURN_ARG(QRectF, overlayRect),
                                      Q_ARG(int, overlay), Q_ARG(QRectF, root->property("zoomTarget").toRectF())));
    QVERIFY(!overlayRect.isEmpty());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centerOf(overlayRect));
    QTRY_COMPARE(backend.selectedOverlay(), overlay);
    QVERIFY(!player->property("playing").toBool());
    QTRY_VERIFY(view->rootObject()->hasActiveFocus());
    QVERIFY(!marker->hasActiveFocus());
    QCOMPARE(backend.timelineSplits().size(), 1);

    // The text is gone at 1.0 s; the mask under it is what a click picks.
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    QVERIFY(showFrame(1000));
    const QRectF maskRect = backend.maskRectInView(mask, player->property("contentRect").toRectF());
    QVERIFY(!maskRect.isEmpty());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centerOf(maskRect));
    QTRY_COMPARE(backend.selectedMask(), mask);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    QVERIFY(!player->property("playing").toBool());

    // Inside a zoom the picture is scaled: the click still finds the mask
    // where it is drawn.
    QVERIFY(backend.addZoomAt(1000) >= 0); // GIF draws zooms everywhere
    {
        QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
        QVERIFY(showFrame(1500));
        QTRY_VERIFY(root->property("zoomFactor").toReal() > 1.01);
        const QRectF target = root->property("zoomTarget").toRectF();
        const QRectF viewport = root->property("zoomView").toRectF();
        const qreal factor = root->property("zoomFactor").toReal();
        const QRectF masked = backend.maskRectInView(mask, player->property("contentRect").toRectF());
        // The visible part of the mask, in zoomed (screen) coordinates.
        const QRectF shown = QRectF(target.x() + (masked.x() - viewport.x()) * factor,
                                    target.y() + (masked.y() - viewport.y()) * factor,
                                    masked.width() * factor, masked.height() * factor).intersected(target);
        QVERIFY(!shown.isEmpty());
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centerOf(shown));
        QTRY_COMPARE(backend.selectedMask(), mask);
    }
    backend.requestDiscard();
}

// #217: a chosen background image is shown as a thumbnail with its name.
void tst_RecordingEditor::backgroundImageShowsThumbnail()
{
    QTemporaryDir dir;
    const QString video = dir.filePath("recording.mp4");
    QVERIFY(writeRecording(video));
    // '#' and a space: a hand-built file URL would cut the name at the '#'.
    const QString picture = dir.filePath("beach #1 background.png");
    QImage image(QSize(64, 40), QImage::Format_ARGB32);
    image.fill(QColor("#2a9d8f"));
    QVERIFY(image.save(picture));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QVERIFY(QMetaObject::invokeMethod(view->rootObject(), "openSection", Q_ARG(QVariant, "appearance")));
    QQuickItem* chosen = item(view, "previewAppearanceImageChosen");
    QVERIFY(chosen);
    QTRY_VERIFY(item(view, "previewAppearancePanel")->isVisible());
    QVERIFY(!chosen->isVisible());
    QVERIFY(backend.setAppearanceImage(QUrl::fromLocalFile(picture)));
    QTRY_VERIFY(chosen->isVisible());
    QQuickItem* thumbnail = item(view, "previewAppearanceImageThumbnail");
    QVERIFY(thumbnail);
    QTRY_COMPARE(thumbnail->property("status").toInt(), 1); // Image.Ready
    QCOMPARE(backend.appearanceImageName(), QStringLiteral("beach #1 background.png"));
    backend.requestDiscard();
}

// #219: a split moved or joined from the keyboard leaves the keyboard on
// the timeline or the editor, so its shortcuts keep working.
void tst_RecordingEditor::splitMarkerKeepsKeyboard()
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
    QVERIFY(backend.splitAt(1000));
    QQuickItem* marker = nullptr;
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    marker->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(marker->hasActiveFocus());
    QTest::keyClick(view, Qt::Key_Right);
    QTRY_COMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1100));
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr && marker->hasActiveFocus());
    backend.undo();
    QCOMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1000)); // one step per press
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr && marker->hasActiveFocus());
    // A held key: the press, its repeats and the release are one undo step.
    QTest::keyPress(view, Qt::Key_Right);
    QTRY_COMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1100));
    for (const qint64 expected : {qint64(1200), qint64(1300)}) {
        QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr && marker->hasActiveFocus());
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, QString(), true);
        QCoreApplication::sendEvent(view, &repeat);
        QTRY_COMPARE(backend.timelineSplits().value(0).toLongLong(), expected);
    }
    // Focus leaves before the key is released: the repeats are already in
    // that one step, so undo and redo give the start and the final place.
    view->rootObject()->forceActiveFocus();
    QTRY_VERIFY(view->rootObject()->hasActiveFocus());
    QTest::keyRelease(view, Qt::Key_Right);
    backend.undo();
    QCOMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1000));
    backend.redo();
    QCOMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1300));
    backend.undo();
    QCOMPARE(backend.timelineSplits().value(0).toLongLong(), qint64(1000));
    QTRY_VERIFY((marker = delegate(view, "previewTimelineSplit0")) != nullptr);
    marker->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(marker->hasActiveFocus());
    QTest::keyClick(view, Qt::Key_Delete);
    QTRY_VERIFY(backend.timelineSplits().isEmpty());
    QTRY_VERIFY(view->rootObject()->hasActiveFocus());
    // The editor's shortcuts work again: S splits at the playhead.
    QVERIFY(seek(view, 800));
    QTest::keyClick(view, Qt::Key_S);
    QTRY_COMPARE(backend.timelineSplits().size(), 1);
    backend.requestDiscard();
}

// Screenshots of every editor state for review (#230). Runs only when
// SNAPTRAY_EDITOR_DESIGN_DIR is set; SNAPTRAY_EDITOR_CONCEPT optionally names
// the concept image to place next to the selected-effect screenshot.
void tst_RecordingEditor::designEvidence_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("dark") << true;
    QTest::newRow("light") << false;
}

void tst_RecordingEditor::designEvidence()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_EDITOR_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_EDITOR_DESIGN_DIR to capture design screenshots");
    QFETCH(bool, dark);
    QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
    // SNAPTRAY_EDITOR_TRANSLATION names a .qm (e.g. build/snaptray_de.qm) to
    // check long translations; its language is added to the file names.
    QTranslator translator;
    const QString qm = qEnvironmentVariable("SNAPTRAY_EDITOR_TRANSLATION");
    if (!qm.isEmpty()) {
        QVERIFY2(translator.load(qm), qPrintable(qm));
        QCoreApplication::installTranslator(&translator);
        theme += QLatin1Char('-') + QFileInfo(qm).completeBaseName().section(QLatin1Char('_'), 1);
    }
    auto removeTranslator = qScopeGuard([&translator] { QCoreApplication::removeTranslator(&translator); });
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
    QDir().mkpath(out);
    QTemporaryDir dir;
    const QString video = dir.filePath("demo-recording.mp4");
    QVERIFY(writeRecording(video));
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QObject* root = view->rootObject();
    const auto settle = [&] {
        for (int i = 0; i < 3; ++i) {
            QSignalSpy frame(view, &QQuickWindow::afterAnimating);
            view->requestUpdate();
            frame.wait(500);
        }
        QTest::qWait(150);
    };
    bool saved = true;
    const auto shot = [&](const QString& name) -> QImage {
        settle();
        const QImage image = view->grabWindow();
        saved = image.save(out + "/" + name + "-" + theme + ".png") && saved;
        return image;
    };
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    shot("01-project-settings");

    // A realistic timeline: two clips, a zoom, a mask and a title.
    QVERIFY(backend.splitAt(1200));
    backend.addZoomAt(1300);
    const int mask = backend.addMask();
    QVERIFY(mask >= 0);
    QVERIFY(backend.setMaskRange(mask, 200, 1000));
    QVERIFY(backend.addTextOverlay(600) >= 0);
    QVERIFY(backend.setOverlayRange(0, 600, 1400));
    backend.setOverlayText(0, QStringLiteral("Update this"));
    backend.setSelectedMask(mask);
    QTRY_VERIFY(item(view, "previewMaskPanel")->isVisible());
    const QImage selected = shot("02-mask-selected");

    backend.setSelectedOverlay(0);
    QTRY_VERIFY(item(view, "previewOverlayPanel")->isVisible());
    shot("03-text-selected");

    if (!backend.zoomSegments().isEmpty()) {
        backend.setSelectedZoom(0);
        QTRY_VERIFY(item(view, "previewZoomPanel")->isVisible());
        shot("04-zoom-selected");
    }

    QVERIFY(QMetaObject::invokeMethod(root, "selectPart", Q_ARG(QVariant, 1)));
    QTRY_VERIFY(item(view, "previewClipPanel")->isVisible());
    shot("05-clip-selected");

    backend.setAppearanceEnabled(true);
    backend.setAppearancePadding(2);
    backend.setAppearanceCorners(2);
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "appearance")));
    QTRY_VERIFY(item(view, "previewAppearancePanel")->isVisible());
    shot("06-canvas-settings");
    backend.setAppearanceEnabled(false);

    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewCropButton"), "clicked"));
    QTRY_VERIFY(item(view, "previewCropOverlay")->property("editing").toBool());
    settle();
    {
        const QRectF content = item(view, "previewVideoPlayer")->property("contentRect").toRectF();
        item(view, "previewCropOverlay")->setProperty("draftRect",
            QRectF(content.x() + content.width() * 0.15, content.y() + content.height() * 0.15,
                   content.width() * 0.6, content.height() * 0.6));
    }
    shot("07-crop-editing");
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewCropOverlay"), "cancel"));

    QObject* popup = root->findChild<QObject*>("previewExportPanel");
    QVERIFY(popup);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QVERIFY(QMetaObject::invokeMethod(root, "openExport"));
    QTRY_VERIFY(popup->property("opened").toBool());
    shot("08-export-mp4");
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    shot("09-export-gif");
    QVERIFY(QMetaObject::invokeMethod(popup, "returnRequested"));
    QTRY_VERIFY(!popup->property("opened").toBool());

    view->resize(640, 480);
    QTRY_VERIFY(!root->property("wideEditor").toBool());
    backend.setSelectedMask(mask);
    root->setProperty("inspectorOpen", true);
    shot("10-narrow-inspector");
    root->setProperty("inspectorOpen", false);
    shot("11-narrow-preview");

    // Side by side with the approved concept's editor (Concept 01, section 03).
    const QString concept = qEnvironmentVariable("SNAPTRAY_EDITOR_CONCEPT");
    if (!concept.isEmpty() && QFile::exists(concept)) {
        const QImage source(concept);
        // The "03 Video editor" section of the 1536×1024 concept board.
        const QRect editorArea(QPointF(36.0 / 1536 * source.width(), 565.0 / 1024 * source.height()).toPoint(),
                               QSizeF(1475.0 / 1536 * source.width(), 420.0 / 1024 * source.height()).toSize());
        const QImage designed = source.copy(editorArea);
        const int width = 1600;
        // Physical pixels on both sides: the window grab carries a device pixel ratio.
        QImage left = designed.scaledToWidth(width, Qt::SmoothTransformation);
        QImage right = selected.scaledToWidth(width, Qt::SmoothTransformation);
        left.setDevicePixelRatio(1.0);
        right.setDevicePixelRatio(1.0);
        const int gap = 24;
        const int label = 44;
        QImage board(width, label + left.height() + gap + label + right.height(), QImage::Format_ARGB32);
        board.fill(dark ? QColor("#17181b") : QColor("#f4f5f7"));
        QPainter painter(&board);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setPixelSize(22);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(dark ? QColor("#eeeeee") : QColor("#212121"));
        painter.drawText(QRect(12, 0, width, label), Qt::AlignVCenter, QStringLiteral("Concept 01 · Video editor"));
        painter.drawImage(0, label, left);
        const int y = label + left.height() + gap;
        painter.drawText(QRect(12, y, width, label), Qt::AlignVCenter,
                         QStringLiteral("SnapTray editor (this PR) · %1").arg(theme));
        painter.drawImage(0, y + label, right);
        painter.end();
        QVERIFY(board.save(out + "/00-concept-comparison-" + theme + ".png"));
    }
    QVERIFY(saved);
    backend.requestDiscard();

}

void tst_RecordingEditor::designEvidenceError_data()
{
    designEvidence_data();
}

// The export error state, for the same review set (#216).
void tst_RecordingEditor::designEvidenceError()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_EDITOR_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_EDITOR_DESIGN_DIR to capture design screenshots");
    QFETCH(bool, dark);
    const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
    QDir().mkpath(out);
    QTemporaryDir dir;
    // An export that cannot read the recording.
    const QString broken = dir.filePath("broken.mp4");
    QFile file(broken);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Incomplete recording data");
    file.close();
    RecordingPreviewBackend failing(broken);
    failing.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* errorView = openPreview(failing);
    QVERIFY(errorView);
    errorView->resize(1200, 800);
    failing.save();
    QTRY_VERIFY_WITH_TIMEOUT(!failing.isProcessing(), 20000);
    QTRY_VERIFY(!failing.errorMessage().isEmpty());
    for (int i = 0; i < 3; ++i) {
        QSignalSpy frame(errorView, &QQuickWindow::afterAnimating);
        errorView->requestUpdate();
        frame.wait(500);
    }
    QTest::qWait(150);
    QVERIFY(errorView->grabWindow().save(out + "/12-export-error-" + theme + ".png"));
    failing.requestDiscard();
}


void tst_RecordingEditor::operationVideo_data()
{
    QTest::addColumn<QString>("scenario");
    QTest::newRow("218-selection") << QStringLiteral("selection");
    QTest::newRow("219-timeline") << QStringLiteral("timeline");
    QTest::newRow("220-flow") << QStringLiteral("flow");
}

// Frames for the operation videos attached to #218, #219 and #220. Runs only
// when SNAPTRAY_EDITOR_VIDEO_DIR is set; each row writes <dir>/<row>/frame-*.png
// (turned into a video with ffmpeg, see docs/developer/ui-design-guidelines.md).
void tst_RecordingEditor::operationVideo()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_EDITOR_VIDEO_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_EDITOR_VIDEO_DIR to record the operation videos");
    QFETCH(QString, scenario);
    const auto originalStyle = ToolbarStyleConfig::loadStyle();
    auto restoreTheme = qScopeGuard([originalStyle] {
        ToolbarStyleConfig::saveStyle(originalStyle);
        DesignSystem::instance().refreshTheme();
    });
    ToolbarStyleConfig::saveStyle(ToolbarStyleType::Dark);
    DesignSystem::instance().refreshTheme();
    QTemporaryDir dir;
    const QString video = dir.filePath("demo-recording.mp4");
    QVERIFY(writeRecording(video, 5)); // 4 s
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    view->resize(1200, 800);
    QTRY_VERIFY(backend.duration() > 0);
    QObject* root = view->rootObject();
    QVERIFY(QMetaObject::invokeMethod(root, "openSection", Q_ARG(QVariant, "")));
    FrameRecorder rec(view, out + "/" + QString::fromLatin1(QTest::currentDataTag()));
    const auto find = [&](const QString& name) -> QQuickItem* {
        QQuickItem* found = nullptr;
        if (!QTest::qWaitFor([&] { return (found = delegate(view, name)) != nullptr && found->isVisible(); }, 3000))
            return nullptr;
        return found;
    };
    const auto at = [&](QQuickItem* target, qreal fx, qreal fy) {
        return target->mapToScene(QPointF(target->width() * fx, target->height() * fy)).toPoint();
    };
    QQuickItem* timeline = item(view, "previewTimeline");
    QQuickItem* scrubber = item(view, "previewScrubber");
    const auto timelinePoint = [&](qint64 ms) {
        QVariant x;
        QMetaObject::invokeMethod(timeline, "posToX", Q_RETURN_ARG(QVariant, x), Q_ARG(QVariant, ms));
        return scrubber->mapToScene(QPointF(x.toReal(), scrubber->property("laneCenterY").toReal())).toPoint();
    };
    QVERIFY(timeline && scrubber);

    if (scenario == QLatin1String("selection")) {
        QVERIFY(backend.splitAt(2000));
        const int mask = backend.addMask();
        QVERIFY(backend.setMaskRange(mask, 300, 1800));
        const int text = backend.addTextOverlay(0);
        backend.setOverlayText(text, QStringLiteral("Release notes"));
        QVERIFY(backend.setOverlayRange(text, 1000, 3200));
        backend.addZoomAt(2600);
        QVERIFY(seek(view, 1300));
        rec.caption(QStringLiteral("#218 · One selection drives the picture, the timeline and the inspector"));
        rec.frame(12);
        rec.caption(QStringLiteral("Click a mask on the timeline: the inspector shows that mask"));
        QQuickItem* maskBlock = find(QStringLiteral("previewMaskSegment%1").arg(mask));
        QVERIFY(maskBlock);
        rec.click(rec.centerOf(maskBlock), 14);
        QCOMPARE(backend.selectedMask(), mask);
        QVERIFY(seek(view, 1300)); // selecting moved the playhead to the mask's start
        rec.caption(QStringLiteral("Click the text on the picture: the same text is selected"));
        QRectF textRect;
        QVERIFY(QMetaObject::invokeMethod(&backend, "overlayRectInView", Q_RETURN_ARG(QRectF, textRect),
                                          Q_ARG(int, text), Q_ARG(QRectF, root->property("zoomTarget").toRectF())));
        rec.click(item(view, "previewVideoClickArea")->mapToScene(textRect.center()).toPoint(), 14);
        QCOMPARE(backend.selectedOverlay(), text);
        if (!backend.zoomSegments().isEmpty()) {
            rec.caption(QStringLiteral("Click a zoom block"));
            QQuickItem* zoomBlock = find(QStringLiteral("previewZoomSegment0"));
            QVERIFY(zoomBlock);
            rec.click(rec.centerOf(zoomBlock), 12);
        }
        rec.caption(QStringLiteral("Double-click a clip on the video lane"));
        rec.doubleClick(timelinePoint(2900), 12);
        QTRY_VERIFY(item(view, "previewClipPanel")->isVisible());
        rec.caption(QStringLiteral("Delete leaves the selected clip out of the export"));
        rec.key(Qt::Key_Delete, Qt::NoModifier, 14);
        rec.caption(QStringLiteral("Undo puts it back; the selection never points at another object"));
        rec.click(rec.centerOf(item(view, "previewUndoButton")), 14);
        rec.caption(QStringLiteral("Typing a time: Space and S are text, not play or split"));
        rec.click(rec.centerOf(find(QStringLiteral("previewMaskSegment%1").arg(mask))), 6);
        QQuickItem* startField = item(view, "previewMaskStartField");
        QTRY_VERIFY(startField->isVisible());
        rec.click(rec.centerOf(startField), 4);
        QMetaObject::invokeMethod(startField, "selectAll");
        rec.type(QStringLiteral("0.5 s"));
        rec.frame(8);
        rec.key(Qt::Key_Return, Qt::NoModifier, 10);
        rec.caption(QStringLiteral("Escape returns to Project settings"));
        rec.key(Qt::Key_Escape, Qt::NoModifier, 6);
        rec.key(Qt::Key_Escape, Qt::NoModifier, 16);
        QTRY_COMPARE(root->property("inspectorKind").toString(), QStringLiteral("project"));
    } else if (scenario == QLatin1String("timeline")) {
        const int mask = backend.addMask();
        QVERIFY(backend.setMaskRange(mask, 500, 1500));
        const int text = backend.addTextOverlay(0);
        backend.setOverlayText(text, QStringLiteral("Title"));
        QVERIFY(backend.setOverlayRange(text, 1800, 3000));
        QVERIFY(seek(view, 900));
        rec.caption(QStringLiteral("#219 · Effects sit on their own lanes and are edited in place"));
        rec.frame(12);
        QQuickItem* maskBlock = find(QStringLiteral("previewMaskSegment%1").arg(mask));
        QVERIFY(maskBlock);
        rec.caption(QStringLiteral("Drag a block to move it"));
        rec.click(rec.centerOf(maskBlock), 4);
        rec.drag(rec.centerOf(maskBlock), rec.centerOf(maskBlock) + QPoint(90, 0));
        maskBlock = find(QStringLiteral("previewMaskSegment%1").arg(mask));
        rec.caption(QStringLiteral("Drag an edge to change its length"));
        rec.drag(at(maskBlock, 1.0, 0.5) - QPoint(2, 0), at(maskBlock, 1.0, 0.5) + QPoint(70, 0));
        rec.caption(QStringLiteral("Each drag is one undo step"));
        rec.click(rec.centerOf(item(view, "previewUndoButton")), 14);
        QQuickItem* textBlock = find(QStringLiteral("previewOverlaySegment%1").arg(text));
        QVERIFY(textBlock);
        rec.caption(QStringLiteral("Escape during a drag cancels it: nothing changes"));
        rec.click(rec.centerOf(textBlock), 4);
        rec.drag(rec.centerOf(textBlock), rec.centerOf(textBlock) - QPoint(120, 0), true);
        QCOMPARE(backend.overlays().at(text).toMap().value("start").toLongLong(), qint64(1800));
        rec.caption(QStringLiteral("Exact times in the inspector, for short or overlapping blocks"));
        rec.click(rec.centerOf(find(QStringLiteral("previewMaskSegment%1").arg(mask))), 6);
        QQuickItem* startField = item(view, "previewMaskStartField");
        QTRY_VERIFY(startField->isVisible());
        rec.click(rec.centerOf(startField), 4);
        QMetaObject::invokeMethod(startField, "selectAll");
        rec.type(QStringLiteral("0.25"));
        rec.key(Qt::Key_Return, Qt::NoModifier, 12);
        rec.caption(QStringLiteral("Split twice and cut the middle clip"));
        rec.key(Qt::Key_Escape, Qt::NoModifier, 4);
        QVERIFY(seek(view, 1200));
        rec.click(rec.centerOf(item(view, "previewSplit")), 6);
        QVERIFY(seek(view, 2400));
        rec.click(rec.centerOf(item(view, "previewSplit")), 6);
        rec.doubleClick(timelinePoint(1800), 6);
        rec.click(rec.centerOf(item(view, "previewPartCut")), 14);
        rec.caption(QStringLiteral("The export time follows cuts: the playhead in a cut part is not exported"));
        rec.click(timelinePoint(1700), 14);
        rec.click(timelinePoint(3000), 14);
        rec.caption(QStringLiteral("2× on the last clip: the export gets shorter"));
        rec.doubleClick(timelinePoint(3200), 6);
        QQuickItem* speed = item(view, "previewPartSpeed");
        QTRY_VERIFY(speed->isVisible());
        QQuickItem* speedControl = speed->findChild<QQuickItem*>("editorChoiceControl");
        rec.click(rec.centerOf(speedControl), 6);
        rec.key(Qt::Key_Down, Qt::NoModifier, 3);
        rec.key(Qt::Key_Down, Qt::NoModifier, 3);
        rec.key(Qt::Key_Return, Qt::NoModifier, 16);
    } else {
        QVERIFY(seek(view, 0)); // the first frame is on screen
        rec.caption(QStringLiteral("#220 · A new recording, from trim to export"));
        rec.frame(12);
        rec.caption(QStringLiteral("Trim: turn on the handles, then drag the start"));
        rec.click(rec.centerOf(item(view, "previewTrimButton")), 8);
        rec.drag(timelinePoint(backend.trimStart()), timelinePoint(backend.trimStart() + 300));
        rec.caption(QStringLiteral("Split at the playhead, then play the second clip at 2×"));
        QVERIFY(seek(view, 2000));
        rec.click(rec.centerOf(item(view, "previewSplit")), 8);
        rec.doubleClick(timelinePoint(2800), 6);
        QQuickItem* speed = item(view, "previewPartSpeed");
        QTRY_VERIFY(speed->isVisible());
        rec.click(rec.centerOf(speed->findChild<QQuickItem*>("editorChoiceControl")), 6);
        rec.key(Qt::Key_Down, Qt::NoModifier, 3);
        rec.key(Qt::Key_Down, Qt::NoModifier, 3);
        rec.key(Qt::Key_Return, Qt::NoModifier, 10);
        rec.caption(QStringLiteral("Canvas: a frame and a background for the whole recording"));
        rec.key(Qt::Key_Escape, Qt::NoModifier, 4);
        rec.click(rec.centerOf(item(view, "previewAppearanceOptions")), 8);
        QQuickItem* toggle = item(view, "previewAppearanceToggle");
        QTRY_VERIFY(toggle->isVisible());
        if (!backend.appearanceEnabled()) rec.click(rec.centerOf(toggle), 8);
        if (QQuickItem* swatch = find(QStringLiteral("previewAppearanceGradient2"))) rec.click(rec.centerOf(swatch), 12);
        QVERIFY(seek(view, 1000));
        rec.caption(QStringLiteral("Add effect: text at the playhead"));
        rec.click(rec.centerOf(item(view, "previewAddEffect")), 6);
        QQuickItem* addText = item(view, "previewAddText");
        QTRY_VERIFY(addText->isVisible());
        rec.click(rec.centerOf(addText), 6);
        QQuickItem* textField = item(view, "previewOverlayText");
        QTRY_VERIFY(textField->isVisible());
        rec.click(rec.centerOf(textField), 4);
        QMetaObject::invokeMethod(textField, "selectAll");
        rec.type(QStringLiteral("New in 2.0"));
        rec.frame(8);
        rec.caption(QStringLiteral("Add effect: a mask, then a zoom"));
        rec.key(Qt::Key_Escape, Qt::NoModifier, 4);
        QVERIFY(seek(view, 1500));
        rec.click(rec.centerOf(item(view, "previewAddEffect")), 6);
        QQuickItem* addMask = item(view, "previewAddMask");
        QTRY_VERIFY(addMask->isVisible());
        rec.click(rec.centerOf(addMask), 10);
        rec.key(Qt::Key_Escape, Qt::NoModifier, 4);
        QVERIFY(seek(view, 3000));
        rec.click(rec.centerOf(item(view, "previewAddEffect")), 6);
        QQuickItem* addZoom = item(view, "previewAddZoom");
        QTRY_VERIFY(addZoom->isVisible());
        rec.click(rec.centerOf(addZoom), 12);
        rec.caption(QStringLiteral("Export: choose GIF, then Export"));
        rec.key(Qt::Key_Escape, Qt::NoModifier, 4);
        rec.click(rec.centerOf(item(view, "previewExportOptions")), 10);
        QObject* panel = root->findChild<QObject*>("previewExportPanel");
        QTRY_VERIFY(panel->property("opened").toBool());
        rec.frame(10);
        QSignalSpy saveRequested(&backend, &RecordingPreviewBackend::saveRequested);
        QQuickItem* saveButton = delegate(view, QStringLiteral("previewSaveButton"));
        QVERIFY(saveButton);
        rec.click(rec.centerOf(saveButton), 2);
        rec.caption(QStringLiteral("Exporting… the edits stay in the recording"));
        for (int i = 0; i < 300 && saveRequested.isEmpty(); ++i) {
            rec.frame();
            QTest::qWait(50);
        }
        QVERIFY(!saveRequested.isEmpty());
        rec.caption(QStringLiteral("Exported: next, SnapTray asks where to save the GIF"));
        rec.frame(16);
        QFile::remove(saveRequested.first().at(0).toString());
    }
    backend.discard();
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
