#include <QtTest/QtTest>

#include <QGuiApplication>

#include "ScreenCanvas.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"
#include "ScreenCanvasSession.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/AnnotationLayer.h"
#include "InlineTextEditor.h"
#include "LaserPointerRenderer.h"
#include "qml/CanvasToolbarViewModel.h"
#include "qml/PinToolOptionsViewModel.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "settings/AnnotationSettingsManager.h"
#include "annotation/ColorPickOverlay.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include <QScreen>
#include <QScopeGuard>

class TestScreenCanvasStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testUsesAuthorityModeByDefault();
    void testOverlayRestoreReturnsArrowToolCursor();
    void testPopupRestoreReturnsEraserCursor();
    void testCustomColorSynchronizesSession_data();
    void testCustomColorSynchronizesSession();
    void selectedArrowStyleIsUndoable();
    void colorPickSamplesScreenWithCanvasHidden();
    void pickedColorEditsSelectionUndoably();
    void polylineMoveIsUndoable();
    void arrowEndpointDragIsUndoable();
    void escapeMidDragRestores();
    void toolInUseStaysOutOfOverflowWithStripCollapsed();
#ifdef Q_OS_MACOS
    void testMacSurfaceAvoidsToolWindowHideBehavior();
#endif
#ifdef Q_OS_LINUX
    void testLinuxSurfaceAvoidsToolWindowHideBehavior();
    void testLinuxSurfaceBypassesWindowManagerForPanelOverlay();
#endif
};

void TestScreenCanvasStyleSync::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas tests in this environment.");
    }
}

void TestScreenCanvasStyleSync::testUsesAuthorityModeByDefault()
{
    ScreenCanvas canvas;
    QCOMPARE(CursorAuthority::instance().modeForWidget(&canvas), CursorSurfaceMode::Authority);
}

void TestScreenCanvasStyleSync::testOverlayRestoreReturnsArrowToolCursor()
{
    ScreenCanvas canvas;
    ToolManager toolManager;
    toolManager.registerDefaultHandlers();
    canvas.setSharedToolManager(&toolManager);

    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    toolManager.setCurrentTool(ToolId::Arrow);
    cursorManager.updateToolCursorForWidget(&canvas);
    cursorManager.reapplyCursorForWidget(&canvas);
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);

    authority.submitWidgetRequest(
        &canvas, QStringLiteral("floating.overlay.toolbar"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&canvas);
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&canvas, QStringLiteral("floating.overlay.toolbar"));
    cursorManager.reapplyCursorForWidget(&canvas);
    QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor);
}

void TestScreenCanvasStyleSync::testPopupRestoreReturnsEraserCursor()
{
    ScreenCanvas canvas;
    ToolManager toolManager;
    toolManager.registerDefaultHandlers();
    canvas.setSharedToolManager(&toolManager);

    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    toolManager.setCurrentTool(ToolId::Eraser);
    toolManager.setWidth(24);
    cursorManager.updateToolCursorForWidget(&canvas);
    cursorManager.reapplyCursorForWidget(&canvas);

    const QCursor toolCursor = canvas.cursor();
    QVERIFY(!toolCursor.pixmap().isNull());

    authority.submitWidgetRequest(
        &canvas, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&canvas);
    QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&canvas, QStringLiteral("floating.popup"));
    cursorManager.reapplyCursorForWidget(&canvas);

    QCOMPARE(canvas.cursor().pixmap().cacheKey(), toolCursor.pixmap().cacheKey());
    QCOMPARE(canvas.cursor().hotSpot(), toolCursor.hotSpot());
}

#ifdef Q_OS_MACOS
void TestScreenCanvasStyleSync::testMacSurfaceAvoidsToolWindowHideBehavior()
{
    ScreenCanvas canvas;
    QVERIFY(canvas.testAttribute(Qt::WA_MacAlwaysShowToolWindow));
    QVERIFY(!canvas.windowFlags().testFlag(Qt::Tool));
}
#endif

#ifdef Q_OS_LINUX
void TestScreenCanvasStyleSync::testLinuxSurfaceAvoidsToolWindowHideBehavior()
{
    ScreenCanvas canvas;
    QVERIFY(!canvas.windowFlags().testFlag(Qt::Tool));
}

void TestScreenCanvasStyleSync::testLinuxSurfaceBypassesWindowManagerForPanelOverlay()
{
    ScreenCanvas canvas;
    QVERIFY(canvas.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
}
#endif

void TestScreenCanvasStyleSync::testCustomColorSynchronizesSession_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("pencil") << 0;
    QTest::newRow("editing-text") << 1;
    QTest::newRow("active-laser") << 2;
}

void TestScreenCanvasStyleSync::testCustomColorSynchronizesSession()
{
    QFETCH(int, mode);
    auto& settings = AnnotationSettingsManager::instance();
    const QColor previous = settings.loadColor();
    const auto restoreColor = qScopeGuard([&] { settings.saveColor(previous); });
    settings.saveColor(Qt::red);
    ScreenCanvasSession session;
    auto* first = new ScreenCanvas;
    auto* second = new ScreenCanvas;
    session.m_surfaces = {first, second};
    session.m_activeSurface = first;
    if (mode == 1) {
        session.m_toolManager->setCurrentTool(ToolId::Text);
        first->inlineTextEditor()->startEditing(QPoint(10, 10), QRect(0, 0, 200, 150));
    } else if (mode == 2) {
        session.m_laserRenderer->startDrawing(QPoint(10, 10));
    }
    const QColor custom(23, 147, 219);
    session.onMoreColorsRequested();
    QVERIFY(session.m_colorPickerDialog);
    session.m_colorPickerDialog->colorSelected(custom);
    session.m_colorPickerDialog->hide();
    QCOMPARE(session.m_toolManager->color(), custom);
    QCOMPARE(session.m_laserRenderer->color(), custom);
    QCOMPARE(session.m_toolOptionsViewModel->currentColor(), custom);
    QCOMPARE(first->inlineTextEditor()->color(), custom);
    QCOMPARE(second->inlineTextEditor()->color(), custom);
    QCOMPARE(settings.loadColor(), custom);
    ScreenCanvasSession reopened;
    QCOMPARE(reopened.m_toolManager->color(), custom);
    QCOMPARE(reopened.m_laserRenderer->color(), custom);
    QCOMPARE(reopened.m_toolOptionsViewModel->currentColor(), custom);
}


void TestScreenCanvasStyleSync::selectedArrowStyleIsUndoable()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto savedArrow = settings.loadArrowStyle();
    const auto restore = qScopeGuard([&] {
        settings.saveColor(savedColor);
        settings.saveArrowStyle(savedArrow);
    });
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    auto* vm = session.m_toolOptionsViewModel;
    QVERIFY(vm->editingSelection());
    vm->handleArrowStyleSelected(int(LineEndStyle::None));
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->lineEndStyle(), LineEndStyle::None);
    QCOMPARE(settings.loadArrowStyle(), savedArrow);
    // The arrow style menu result and the custom colour dialog also edit the selection.
    session.onLineStyleChanged(LineStyle::Dashed);
    QCOMPARE(arrow()->lineStyle(), LineStyle::Dashed);
    session.onMoreColorsRequested();
    QVERIFY(session.m_colorPickerDialog);
    const QColor custom(23, 147, 219);
    session.m_colorPickerDialog->colorSelected(custom);
    session.m_colorPickerDialog->hide();
    QCOMPARE(arrow()->color(), custom);
    QCOMPARE(settings.loadColor(), savedColor);
    session.handleUndoAction(ToolId::Undo);
    QCOMPARE(arrow()->color(), QColor(Qt::red));
    session.handleUndoAction(ToolId::Undo);
    session.handleUndoAction(ToolId::Undo);
    QCOMPARE(arrow()->lineEndStyle(), LineEndStyle::EndArrow);
    session.handleRedoAction(ToolId::Redo);
    QCOMPARE(arrow()->lineEndStyle(), LineEndStyle::None);
    // Picking another tool ends the selection; the strip returns to that tool.
    session.handlePersistentToolClick(ToolId::Pencil);
    QVERIFY(!vm->editingSelection());
    QCOMPARE(session.m_annotationLayer->selectedIndex(), -1);
}

// #271: the eyedropper samples a snapshot of the screen taken with the
// canvas (and its annotations) hidden.
void TestScreenCanvasStyleSync::colorPickSamplesScreenWithCanvasHidden()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();
    session.m_isOpen = true;
    session.m_bgMode = CanvasBackgroundMode::Screen;
    ScreenCanvas surface(&session);
    surface.initializeForScreenSurface(
        screen, ScreenCanvasSession::virtualDesktopGeometry(QGuiApplication::screens()));
    surface.setSharedToolManager(session.m_toolManager);
    session.m_surfaces = {&surface};
    session.m_activeSurface = &surface;
    session.m_activationScreen = screen;
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(0, 5), QPoint(200, 5), Qt::red, 12));
    const auto cleanup = qScopeGuard([&] {
        session.m_surfaces.clear();
        session.m_activeSurface = nullptr;
        session.m_isOpen = false;
    });
    surface.show();
    QCoreApplication::processEvents();

    const QColor desktop(12, 34, 56);
    bool canvasHidden = false;
    session.m_screenSnapshotProvider = [&](QScreen* target) {
        canvasHidden = !surface.isVisible();
        QPixmap pixmap(target->geometry().size() * target->devicePixelRatio());
        pixmap.fill(desktop);
        pixmap.setDevicePixelRatio(target->devicePixelRatio());
        return pixmap;
    };

    const ColorPickSource source = session.colorPickSource(screen);
    QVERIFY(source.isValid());
    QVERIFY(canvasHidden);
    QVERIFY(surface.isVisible());
    QCOMPARE(source.sampleRect, screen->geometry());
    QCOMPARE(source.image.devicePixelRatio(), screen->devicePixelRatio());
    QCOMPARE(ColorPick::colorAt(source.image, QPoint(10, 5)), desktop);

    // A whiteboard samples as its fill color.
    session.m_bgMode = CanvasBackgroundMode::Whiteboard;
    QCOMPARE(ColorPick::colorAt(session.colorPickSource(screen).image, QPoint(10, 5)),
             QColor(Qt::white));
}

void TestScreenCanvasStyleSync::pickedColorEditsSelectionUndoably()
{
    auto& settings = AnnotationSettingsManager::instance();
    const QColor savedColor = settings.loadColor();
    const auto restore = qScopeGuard([&] { settings.saveColor(savedColor); });
    settings.saveColor(Qt::red);
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    auto* vm = session.m_toolOptionsViewModel;
    const QColor picked(0x1a, 0x73, 0xe8);

    session.applyPickedColor(picked);
    QCOMPARE(vm->currentColor(), picked);
    QCOMPARE(session.m_toolManager->color(), picked);
    QCOMPARE(settings.loadColor(), picked);

    settings.saveColor(Qt::red);
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::green, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(vm->editingSelection());
    session.applyPickedColor(picked);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QCOMPARE(arrow()->color(), picked);
    QCOMPARE(settings.loadColor(), QColor(Qt::red));
    session.handleUndoAction(ToolId::Undo);
    QCOMPARE(arrow()->color(), QColor(Qt::green));
}

void TestScreenCanvasStyleSync::toolInUseStaysOutOfOverflowWithStripCollapsed()
{
    // #224 review: Whiteboard/Blackboard take the highlight, and collapsing the
    // options strip clears it from the tool; the tool in use must still be kept
    // out of More.
    ScreenCanvasSession session;
    auto* vm = session.m_toolbarViewModel;
    session.m_currentToolId = ToolId::EmojiSticker;
    session.m_laserPointerActive = false;
    for (const auto mode : {CanvasBackgroundMode::Whiteboard, CanvasBackgroundMode::Blackboard,
                            CanvasBackgroundMode::Screen}) {
        session.m_bgMode = mode;
        for (const bool stripShown : {true, false}) {
            session.m_showSubToolbar = stripShown;
            session.updateQmlToolbarState();
            QCOMPARE(vm->overflowKeepTool(), int(ToolId::EmojiSticker));
        }
    }
    session.m_bgMode = CanvasBackgroundMode::Whiteboard;
    session.updateQmlToolbarState();
    QCOMPARE(vm->activeTool(), int(ToolId::CanvasWhiteboard));
}

// #237: moving a polyline is one undo step.
void TestScreenCanvasStyleSync::polylineMoveIsUndoable()
{
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    session.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{{20, 80}, {70, 30}, {120, 80}}, Qt::red, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    auto line = [&] { return static_cast<PolylineAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QVERIFY(session.handlePolylineAnnotationPress(QPoint(45, 55)));
    session.handlePolylineAnnotationMove(QPoint(55, 55));
    session.handlePolylineAnnotationMove(QPoint(65, 60));
    session.handlePolylineAnnotationRelease(QPoint(65, 60));
    QCOMPARE(line()->points().first(), QPoint(40, 85));
    session.m_annotationLayer->undo();
    QCOMPARE(session.m_annotationLayer->itemCount(), size_t(1));   // the move, not the add
    QCOMPARE(line()->points().first(), QPoint(20, 80));
}

void TestScreenCanvasStyleSync::arrowEndpointDragIsUndoable()
{
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QVERIFY(session.handleArrowAnnotationPress(QPoint(120, 20)));
    session.handleArrowAnnotationMove(QPoint(140, 50));
    session.handleArrowAnnotationRelease(QPoint(140, 50));
    QCOMPARE(arrow()->end(), QPoint(140, 50));
    session.m_annotationLayer->undo();
    QCOMPARE(session.m_annotationLayer->itemCount(), size_t(1));
    QCOMPARE(arrow()->end(), QPoint(120, 20));
}

// Escape during a drag puts the annotation back; the canvas stays open.
void TestScreenCanvasStyleSync::escapeMidDragRestores()
{
    ScreenCanvasSession session;
    auto* surface = new ScreenCanvas;
    session.m_surfaces = {surface};
    session.m_activeSurface = surface;
    session.m_annotationLayer->addItem(
        std::make_unique<ArrowAnnotation>(QPoint(20, 20), QPoint(120, 20), Qt::red, 3));
    session.m_annotationLayer->setSelectedIndex(0);
    QSignalSpy closed(&session, &ScreenCanvasSession::closed);
    const auto token = session.m_annotationLayer->historyStateToken();
    auto arrow = [&] { return static_cast<ArrowAnnotation*>(session.m_annotationLayer->itemAt(0)); };
    QVERIFY(session.handleArrowAnnotationPress(QPoint(40, 20)));
    session.handleArrowAnnotationMove(QPoint(50, 50));
    QCOMPARE(arrow()->start(), QPoint(30, 50));
    QVERIFY(!session.m_consumeNextToolRelease);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    session.handleSurfaceKeyPress(surface, &escape);
    QVERIFY(session.m_consumeNextToolRelease);   // the held button's release places nothing
    QCOMPARE(arrow()->start(), QPoint(20, 20));
    QCOMPARE(session.m_annotationLayer->historyStateToken(), token);
    QCOMPARE(closed.count(), 0);
    session.handleArrowAnnotationMove(QPoint(70, 70));
    session.handleArrowAnnotationRelease(QPoint(70, 70));
    QCOMPARE(arrow()->start(), QPoint(20, 20));
}

QTEST_MAIN(TestScreenCanvasStyleSync)
#include "tst_StyleSync.moc"
