#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include <functional>

#include "qml/RecordingPreviewBackend.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "settings/RecordingSettingsManager.h"

// The preview's pointer controls (#160) against the real QML.
class tst_RecordingPreviewPointer : public QObject
{
    Q_OBJECT

private slots:
    void controlsFollowThePointerData();
    void controlsHiddenWithoutPointerData();
    void overlayFollowsEachFrameTime();
    void advancedPointerOptions();
    void keysPanelEditsTheHints();

private:
    static QQuickView* openPreview(RecordingPreviewBackend& backend);
    static QQuickItem* item(QQuickView* view, const char* name);
};

QQuickView* tst_RecordingPreviewPointer::openPreview(RecordingPreviewBackend& backend)
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

QQuickItem* tst_RecordingPreviewPointer::item(QQuickView* view, const char* name)
{
    return view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(name));
}

void tst_RecordingPreviewPointer::controlsFollowThePointerData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("missing.mp4"));
    {
        SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(video));
        SnapTray::CursorTimelineInfo info;
        info.frameSize = QSize(320, 240);
        info.clicksAvailable = false; // positions only: no click highlights
        QVERIFY(writer.open(info));
        writer.addSample({0, QPoint(10, 10), true, 0});
        QVERIFY(writer.finish());
    }
    RecordingPreviewBackend backend(video);
    QVERIFY(backend.hasPointerData());
    QQuickView* view = openPreview(backend);
    QVERIFY(view);

    QQuickItem* toggle = item(view, "previewPointerToggle");
    QQuickItem* size = item(view, "previewPointerSize");
    QQuickItem* smoothing = item(view, "previewPointerSmoothing");
    QQuickItem* clicks = item(view, "previewClickRipple");
    QVERIFY(toggle && size && smoothing && clicks);
    QVERIFY(toggle->isVisible());
    QVERIFY(item(view, "previewCursorOverlay"));
    QCOMPARE(clicks->isEnabled(), false); // nothing to highlight without recorded clicks

    if (!backend.cursorEffectsAvailable()) {
        QVERIFY(!toggle->isEnabled());
        QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
    }
    const bool shown = backend.showCursor();
    QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QCOMPARE(backend.showCursor(), !shown);
    if (!backend.showCursor()) QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
    QVERIFY(backend.showCursor());
    QVERIFY(size->isEnabled());

    const qreal before = backend.cursorScale();
    QVERIFY(QMetaObject::invokeMethod(size, "clicked"));
    QVERIFY(backend.cursorScale() != before);
    QVERIFY(QMetaObject::invokeMethod(smoothing, "clicked"));
    QCOMPARE(backend.cursorSmoothing(), 1);
    // Undo walks back through the look, one change at a time.
    backend.undo();
    QCOMPARE(backend.cursorSmoothing(), 0);
    backend.undo();
    QCOMPARE(backend.cursorScale(), before);
    backend.requestDiscard();
}

void tst_RecordingPreviewPointer::advancedPointerOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("missing.mp4"));
    {
        SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(video));
        SnapTray::CursorTimelineInfo info;
        info.frameSize = QSize(320, 240);
        QVERIFY(writer.open(info));
        writer.addSample({0, QPoint(10, 10), true, 0});
        QVERIFY(writer.finish());
    }
    // The look is remembered: leave it as the other tests expect it.
    const QJsonObject rememberedLook = RecordingSettingsManager::instance().pointerEffects();
    auto restore = qScopeGuard([rememberedLook]() {
        RecordingSettingsManager::instance().setPointerEffects(rememberedLook);
    });
    RecordingPreviewBackend backend(video);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    if (!backend.cursorEffectsAvailable()) {
        backend.requestDiscard();
        QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
    }
    QQuickItem* open = item(view, "previewPointerOptions");
    QQuickItem* panel = item(view, "previewPointerPanel");
    QQuickItem* formatRow = item(view, "previewFormatRow");
    QVERIFY(open && panel && formatRow);
    QVERIFY(!panel->isVisible());
    QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(!formatRow->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(item(view, "previewPointerIdle"), "clicked"));
    QCOMPARE(backend.cursorIdleHide(), 1000);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewPointerBlur"), "clicked"));
    QCOMPARE(backend.cursorMotionBlur(), 1);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewPointerStillEnd"), "clicked"));
    QCOMPARE(backend.cursorStillEnd(), 500);

    // A hidden range at the playhead opens its own editor.
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewPointerHideHere"), "clicked"));
    QCOMPARE(backend.pointerHiddenRanges().size(), 1);
    QCOMPARE(backend.selectedPointerHide(), 0);
    QQuickItem* hidePanel = item(view, "previewPointerHidePanel");
    QVERIFY(hidePanel);
    QTRY_VERIFY(hidePanel->isVisible());
    QVERIFY(!panel->isVisible());
    // Repeater delegates are only reachable through the visual tree.
    std::function<QQuickItem*(QQuickItem*)> walk = [&walk](QQuickItem* node) -> QQuickItem* {
        if (node->objectName() == QLatin1String("previewPointerHideSegment0")) return node;
        for (QQuickItem* child : node->childItems()) {
            if (QQuickItem* found = walk(child)) return found;
        }
        return nullptr;
    };
    QTRY_VERIFY(walk(view->contentItem()));
    QVERIFY(backend.setPointerHideRange(0, 500, 1500));
    QVERIFY(!backend.setPointerHideRange(0, 500, 520)); // shorter than the minimum
    QCOMPARE(backend.pointerHiddenRanges().first().toMap().value(QStringLiteral("start")).toLongLong(), 500);

    // The look is remembered for the next recording; the ranges are not.
    const auto remembered = SnapTray::CursorEffectSettings::fromJson(RecordingSettingsManager::instance().pointerEffects());
    QCOMPARE(remembered.idleHideMs, 1000);
    QVERIFY(remembered.hiddenRanges.empty());

    // Undo walks back the range edit, then the new range.
    backend.undo();
    QCOMPARE(backend.pointerHiddenRanges().first().toMap().value(QStringLiteral("start")).toLongLong(), 0);
    backend.undo();
    QVERIFY(backend.pointerHiddenRanges().isEmpty());
    QCOMPARE(backend.selectedPointerHide(), -1);
    QTRY_VERIFY(!hidePanel->isVisible());

    backend.requestDiscard();
}

// #187: the Keys button shows only for a recording with keys; its panel sets
// the hint look, undoably, and remembers it.
void tst_RecordingPreviewPointer::keysPanelEditsTheHints()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("missing.mp4"));
    const QJsonObject remembered = RecordingSettingsManager::instance().keyOverlay();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setKeyOverlay(remembered); });
    RecordingSettingsManager::instance().setKeyOverlay({});
    {
        RecordingPreviewBackend withoutKeys(video);
        QVERIFY(!withoutKeys.hasKeyData());
        QQuickView* view = openPreview(withoutKeys);
        QVERIFY(view);
        QQuickItem* keys = item(view, "previewKeyOptions");
        QVERIFY(keys);
        QVERIFY(!keys->isVisible());
        withoutKeys.requestDiscard();
    }
    {
        SnapTray::KeyTimelineWriter writer(SnapTray::KeyTimelineSidecar::pathFor(video));
        QVERIFY(writer.open({SnapTray::KeyRecordingMode::Shortcuts, false}));
        writer.add({100, Qt::Key_C, Qt::ControlModifier, {}});
        QVERIFY(writer.finish());
    }
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQuickItem* keys = item(view, "previewKeyOptions");
    QQuickItem* panel = item(view, "previewKeysPanel");
    QVERIFY(keys && panel);
    QTRY_VERIFY(keys->isVisible());
    QVERIFY(keys->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(keys, "clicked"));
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysPosition"), "clicked"));
    QCOMPARE(backend.keyOverlayPosition(), 1);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysSize"), "clicked"));
    QCOMPARE(backend.keyOverlaySize(), 2);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysStyle"), "clicked"));
    QCOMPARE(backend.keyOverlayStyle(), 1);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysDuration"), "clicked"));
    QCOMPARE(backend.keyOverlayDisplayMs(), 2500);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysToggle"), "clicked"));
    QVERIFY(!backend.keyOverlayEnabled());
    QVERIFY(!backend.keyOverlayActive());
    const auto saved = SnapTray::KeyOverlaySettings::fromJson(RecordingSettingsManager::instance().keyOverlay());
    QCOMPARE(saved, backend.keyOverlay());
    backend.undo();
    QVERIFY(backend.keyOverlayEnabled());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewKeysDone"), "clicked"));
    QTRY_VERIFY(!panel->isVisible());
    backend.requestDiscard();
}

void tst_RecordingPreviewPointer::controlsHiddenWithoutPointerData()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    QVERIFY(!backend.hasPointerData());
    QVERIFY(!backend.cursorEffectsActive());
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQuickItem* toggle = item(view, "previewPointerToggle");
    QVERIFY(toggle);
    QVERIFY(!toggle->isVisible());
    backend.requestDiscard();
}

void tst_RecordingPreviewPointer::overlayFollowsEachFrameTime()
{
    // PR #175 review: the overlay followed the player's position, which the
    // macOS and Windows players report only every 100 ms, so the preview
    // pointer moved at about 10 fps against full-rate video. It follows the
    // time of each displayed frame.
    QTemporaryDir dir;
    const QString video = dir.filePath(QStringLiteral("missing.mp4"));
    {
        SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(video));
        SnapTray::CursorTimelineInfo info;
        info.frameSize = QSize(320, 240);
        QVERIFY(writer.open(info));
        writer.addSample({0, QPoint(10, 10), true, 0});
        QVERIFY(writer.finish());
    }
    RecordingPreviewBackend backend(video);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQuickItem* player = item(view, "previewVideoPlayer");
    QQuickItem* overlay = item(view, "previewCursorOverlay");
    QVERIFY(player && overlay);
    QImage frame(320, 240, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::gray);
    for (const qint64 t : {33, 67, 100, 133}) {
        // A frame with its presentation time and no position report.
        QVERIFY(QMetaObject::invokeMethod(player, "onFrameReady", Qt::DirectConnection,
                                          Q_ARG(QImage, frame), Q_ARG(qint64, t)));
        QCOMPARE(player->property("framePosition").toLongLong(), t);
        QCOMPARE(overlay->property("position").toLongLong(), t);
    }
}

QTEST_MAIN(tst_RecordingPreviewPointer)
#include "tst_RecordingPreviewPointer.moc"
