#include <QtTest/QtTest>

#include <QGuiApplication>

#include "ScreenCanvas.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"

class TestScreenCanvasStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testUsesAuthorityModeByDefault();
    void testOverlayRestoreReturnsArrowToolCursor();
    void testPopupRestoreReturnsEraserCursor();
#ifdef Q_OS_MACOS
    void testMacSurfaceAvoidsToolWindowHideBehavior();
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

QTEST_MAIN(TestScreenCanvasStyleSync)
#include "tst_StyleSync.moc"
