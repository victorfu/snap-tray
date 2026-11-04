#include <QtTest>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QDir>
#include <functional>
#include <QScopeGuard>
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
    evidence(view, "editor-selected");
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewExportOptions"), "clicked"));
    QObject* popup = view->rootObject()->findChild<QObject*>("previewExportPanel");
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("opened").toBool());
    QCOMPARE(backend.selectedMask(), mask); // opening export does not lose the selection
    QCOMPARE(player->size(), size);
    evidence(view, "editor-export");
    QVERIFY(QMetaObject::invokeMethod(popup, "returnRequested"));
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

QTEST_MAIN(tst_RecordingEditor)
#include "tst_RecordingEditor.moc"
