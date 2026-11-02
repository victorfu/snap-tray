#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include "qml/RecordingPreviewBackend.h"
#include "recording/CursorTimeline.h"

// The preview's pointer controls (#160) against the real QML.
class tst_RecordingPreviewPointer : public QObject
{
    Q_OBJECT

private slots:
    void controlsFollowThePointerData();
    void controlsHiddenWithoutPointerData();

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

QTEST_MAIN(tst_RecordingPreviewPointer)
#include "tst_RecordingPreviewPointer.moc"
