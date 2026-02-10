#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "tools/handlers/ArrowToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

/**
 * @brief Tests for ArrowToolHandler class
 *
 * Covers:
 * - Tool identification
 * - Drag mode (straight arrow creation)
 * - Click mode (polyline creation)
 * - Mouse event handling
 * - State management
 * - Escape key handling
 * - Capability flags
 */
class TestArrowToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Tool identification tests
    void testToolId();

    // Capability tests
    void testSupportsColor();
    void testSupportsWidth();
    void testSupportsArrowStyle();
    void testSupportsLineStyle();

    // State tests
    void testInitialState();
    void testIsDrawing_BeforeDraw();
    void testIsDrawing_DuringDrag();

    // Activation tests
    void testOnActivate_ResetsState();
    void testOnDeactivate_CancelsDrawing();

    // Drag mode tests (straight arrow)
    void testDragMode_StartsOnPress();
    void testDragMode_UpdatesOnMove();
    void testDragMode_CreatesArrowOnRelease();

    // Click mode tests (polyline)
    void testClickMode_AddsPointsOnClick();

    // Preview tests
    void testDrawPreview_DragMode();
    void testDrawPreview_ClickMode();

    // Cancellation tests
    void testCancelDrawing_DragMode();
    void testCancelDrawing_ClickMode();
    void testHandleEscape_DragMode();
    void testHandleEscape_NotDrawing();

private:
    ArrowToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    int m_repaintCount = 0;
};

void TestArrowToolHandler::init()
{
    m_handler = new ArrowToolHandler();
    m_layer = new AnnotationLayer();
    m_context = new ToolContext();

    m_context->annotationLayer = m_layer;
    m_context->color = Qt::red;
    m_context->width = 3;
    m_context->arrowStyle = LineEndStyle::EndArrow;
    m_context->lineStyle = LineStyle::Solid;
    m_context->shiftPressed = false;

    m_repaintCount = 0;
    m_context->requestRepaint = [this]() {
        m_repaintCount++;
    };
}

void TestArrowToolHandler::cleanup()
{
    delete m_handler;
    delete m_context;
    delete m_layer;
    m_handler = nullptr;
    m_context = nullptr;
    m_layer = nullptr;
}

// ============================================================================
// Tool Identification Tests
// ============================================================================

void TestArrowToolHandler::testToolId()
{
    QCOMPARE(m_handler->toolId(), ToolId::Arrow);
}

// ============================================================================
// Capability Tests
// ============================================================================

void TestArrowToolHandler::testSupportsColor()
{
    QVERIFY(m_handler->supportsColor());
}

void TestArrowToolHandler::testSupportsWidth()
{
    QVERIFY(m_handler->supportsWidth());
}

void TestArrowToolHandler::testSupportsArrowStyle()
{
    QVERIFY(m_handler->supportsArrowStyle());
}

void TestArrowToolHandler::testSupportsLineStyle()
{
    QVERIFY(m_handler->supportsLineStyle());
}

// ============================================================================
// State Tests
// ============================================================================

void TestArrowToolHandler::testInitialState()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testIsDrawing_BeforeDraw()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testIsDrawing_DuringDrag()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QVERIFY(m_handler->isDrawing());
}

// ============================================================================
// Activation Tests
// ============================================================================

void TestArrowToolHandler::testOnActivate_ResetsState()
{
    // Start drawing, then activate
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    m_handler->onActivate(m_context);

    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testOnDeactivate_CancelsDrawing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    size_t initialItemCount = m_layer->itemCount();
    m_handler->onDeactivate(m_context);

    // Should not add item when deactivating mid-draw
    QCOMPARE(m_layer->itemCount(), initialItemCount);
}

// ============================================================================
// Drag Mode Tests (Straight Arrow)
// ============================================================================

void TestArrowToolHandler::testDragMode_StartsOnPress()
{
    int initialRepaintCount = m_repaintCount;

    m_handler->onMousePress(m_context, QPoint(100, 100));

    QVERIFY(m_repaintCount > initialRepaintCount);
}

void TestArrowToolHandler::testDragMode_UpdatesOnMove()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    int repaintAfterPress = m_repaintCount;

    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QVERIFY(m_repaintCount > repaintAfterPress);
    QVERIFY(m_handler->isDrawing());
}

void TestArrowToolHandler::testDragMode_CreatesArrowOnRelease()
{
    size_t initialItemCount = m_layer->itemCount();

    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 200));
    m_handler->onMouseRelease(m_context, QPoint(200, 200));

    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Click Mode Tests (Polyline)
// ============================================================================

void TestArrowToolHandler::testClickMode_AddsPointsOnClick()
{
    // Enter click mode
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseRelease(m_context, QPoint(100, 100));

    int repaintAfterFirstClick = m_repaintCount;

    // Add another point
    m_handler->onMousePress(m_context, QPoint(150, 150));

    QVERIFY(m_repaintCount > repaintAfterFirstClick);
}

// ============================================================================
// Preview Tests
// ============================================================================

void TestArrowToolHandler::testDrawPreview_DragMode()
{
    m_handler->onMousePress(m_context, QPoint(50, 50));
    m_handler->onMouseMove(m_context, QPoint(150, 50));

    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    m_handler->drawPreview(painter);
    painter.end();

    // Should have drawn something
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestArrowToolHandler::testDrawPreview_ClickMode()
{
    // Enter click mode
    m_handler->onMousePress(m_context, QPoint(50, 50));
    m_handler->onMouseRelease(m_context, QPoint(50, 50));

    // Move to show preview line
    m_handler->onMouseMove(m_context, QPoint(150, 50));

    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    m_handler->drawPreview(painter);
    painter.end();

    // Should have drawn something
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

// ============================================================================
// Cancellation Tests
// ============================================================================

void TestArrowToolHandler::testCancelDrawing_DragMode()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QVERIFY(m_handler->isDrawing());

    m_handler->cancelDrawing();

    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testCancelDrawing_ClickMode()
{
    // Enter click mode
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseRelease(m_context, QPoint(100, 100));

    m_handler->cancelDrawing();

    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testHandleEscape_DragMode()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    bool handled = m_handler->handleEscape(m_context);

    QVERIFY(handled);
    QVERIFY(!m_handler->isDrawing());
}

void TestArrowToolHandler::testHandleEscape_NotDrawing()
{
    bool handled = m_handler->handleEscape(m_context);

    QVERIFY(!handled);
}

QTEST_MAIN(TestArrowToolHandler)
#include "tst_ArrowToolHandler.moc"
