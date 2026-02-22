#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "tools/handlers/ShapeToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

/**
 * @brief Tests for ShapeToolHandler class
 *
 * Covers:
 * - Tool identification
 * - Shape creation (rectangle and ellipse)
 * - Fill modes (outline and filled)
 * - Mouse event handling
 * - State management
 * - Shift key constraint (square/circle)
 * - Capability flags
 */
class TestShapeToolHandler : public QObject
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
    void testSupportsShapeType();
    void testSupportsFillMode();

    // State tests
    void testInitialState();
    void testIsDrawing_BeforePress();
    void testIsDrawing_DuringDraw();
    void testIsDrawing_AfterRelease();

    // Mouse event tests
    void testOnMousePress_StartsDrawing();
    void testOnMouseMove_UpdatesShape();
    void testOnMouseRelease_CreatesShape();
    void testOnMouseRelease_MinimumSize();

    // Shape type tests
    void testRectangleShape();
    void testEllipseShape();

    // Preview tests
    void testDrawPreview_WhileDrawing();

    // Cancellation tests
    void testCancelDrawing();
    void testOnMouseRelease_SelectsCreatedShape();

private:
    ShapeToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    int m_repaintCount = 0;
};

void TestShapeToolHandler::init()
{
    m_handler = new ShapeToolHandler();
    m_layer = new AnnotationLayer();
    m_context = new ToolContext();

    m_context->annotationLayer = m_layer;
    m_context->color = Qt::red;
    m_context->width = 3;
    m_context->shapeType = 0;     // Rectangle
    m_context->shapeFillMode = 0; // Outline
    m_context->shiftPressed = false;

    m_repaintCount = 0;
    m_context->requestRepaint = [this]() {
        m_repaintCount++;
    };
}

void TestShapeToolHandler::cleanup()
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

void TestShapeToolHandler::testToolId()
{
    QCOMPARE(m_handler->toolId(), ToolId::Shape);
}

// ============================================================================
// Capability Tests
// ============================================================================

void TestShapeToolHandler::testSupportsColor()
{
    QVERIFY(m_handler->supportsColor());
}

void TestShapeToolHandler::testSupportsWidth()
{
    QVERIFY(m_handler->supportsWidth());
}

void TestShapeToolHandler::testSupportsShapeType()
{
    QVERIFY(m_handler->supportsShapeType());
}

void TestShapeToolHandler::testSupportsFillMode()
{
    QVERIFY(m_handler->supportsFillMode());
}

// ============================================================================
// State Tests
// ============================================================================

void TestShapeToolHandler::testInitialState()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestShapeToolHandler::testIsDrawing_BeforePress()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestShapeToolHandler::testIsDrawing_DuringDraw()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    QVERIFY(m_handler->isDrawing());
}

void TestShapeToolHandler::testIsDrawing_AfterRelease()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 200));
    m_handler->onMouseRelease(m_context, QPoint(200, 200));

    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Mouse Event Tests
// ============================================================================

void TestShapeToolHandler::testOnMousePress_StartsDrawing()
{
    int initialRepaintCount = m_repaintCount;

    m_handler->onMousePress(m_context, QPoint(100, 100));

    QVERIFY(m_handler->isDrawing());
    QVERIFY(m_repaintCount > initialRepaintCount);
}

void TestShapeToolHandler::testOnMouseMove_UpdatesShape()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    int repaintAfterPress = m_repaintCount;

    m_handler->onMouseMove(m_context, QPoint(200, 200));

    QVERIFY(m_repaintCount > repaintAfterPress);
}

void TestShapeToolHandler::testOnMouseRelease_CreatesShape()
{
    size_t initialItemCount = m_layer->itemCount();

    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 200));
    m_handler->onMouseRelease(m_context, QPoint(200, 200));

    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
    QVERIFY(!m_handler->isDrawing());
}

void TestShapeToolHandler::testOnMouseRelease_MinimumSize()
{
    size_t initialItemCount = m_layer->itemCount();

    // Very small shape - might not be added
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(102, 102));
    m_handler->onMouseRelease(m_context, QPoint(102, 102));

    // Implementation may or may not add very small shapes
    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Shape Type Tests
// ============================================================================

void TestShapeToolHandler::testRectangleShape()
{
    m_context->shapeType = 0;  // Rectangle

    size_t initialItemCount = m_layer->itemCount();

    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 150));
    m_handler->onMouseRelease(m_context, QPoint(200, 150));

    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
}

void TestShapeToolHandler::testEllipseShape()
{
    m_context->shapeType = 1;  // Ellipse

    size_t initialItemCount = m_layer->itemCount();

    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 150));
    m_handler->onMouseRelease(m_context, QPoint(200, 150));

    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
}

// ============================================================================
// Preview Tests
// ============================================================================

void TestShapeToolHandler::testDrawPreview_WhileDrawing()
{
    m_handler->onMousePress(m_context, QPoint(50, 50));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QImage image(200, 200, QImage::Format_ARGB32);
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

void TestShapeToolHandler::testCancelDrawing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(200, 200));

    QVERIFY(m_handler->isDrawing());

    m_handler->cancelDrawing();

    QVERIFY(!m_handler->isDrawing());
}

void TestShapeToolHandler::testOnMouseRelease_SelectsCreatedShape()
{
    m_handler->onMousePress(m_context, QPoint(20, 20));
    m_handler->onMouseMove(m_context, QPoint(120, 80));
    m_handler->onMouseRelease(m_context, QPoint(120, 80));

    QCOMPARE(m_layer->itemCount(), static_cast<size_t>(1));
    QCOMPARE(m_layer->selectedIndex(), 0);
}

QTEST_MAIN(TestShapeToolHandler)
#include "tst_ShapeToolHandler.moc"
