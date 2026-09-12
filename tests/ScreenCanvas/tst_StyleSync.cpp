#include <QtTest/QtTest>

#include <QGuiApplication>

#include "ScreenCanvas.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"
#include "ScreenCanvasSession.h"
#include "InlineTextEditor.h"
#include "LaserPointerRenderer.h"
#include "qml/PinToolOptionsViewModel.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "settings/AnnotationSettingsManager.h"
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

QTEST_MAIN(TestScreenCanvasStyleSync)
#include "tst_StyleSync.moc"
