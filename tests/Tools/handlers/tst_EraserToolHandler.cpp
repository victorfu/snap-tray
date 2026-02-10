#include <QtTest/QtTest>
#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ErasedItemsGroup.h"
#include "annotations/PencilStroke.h"

/**
 * @brief Tests for EraserToolHandler class
 *
 * Covers:
 * - Tool identification
 * - Eraser size/width
 * - Mouse event handling
 * - Intersection detection
 * - State management
 * - Capability flags
 */
class TestEraserToolHandler : public QObject
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

    // State tests
    void testInitialState();
    void testIsDrawing_BeforePress();
    void testIsDrawing_DuringErase();
    void testIsDrawing_AfterRelease();

    // Mouse event tests
    void testOnMousePress_StartsErasing();
    void testOnMouseMove_ContinuesErasing();
    void testOnMouseRelease_StopsErasing();

    // Cancellation tests
    void testCancelDrawing();

    // Eraser functionality tests
    void testErasesIntersectingItems();
    void testDoesNotEraseNonIntersecting();

private:
    EraserToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    int m_repaintCount = 0;

    void addTestStroke(const QVector<QPointF>& points);
};

void TestEraserToolHandler::init()
{
    m_handler = new EraserToolHandler();
    m_layer = new AnnotationLayer();
    m_context = new ToolContext();

    m_context->annotationLayer = m_layer;
    m_context->color = Qt::red;  // Not used by eraser
    m_context->width = 20;       // Eraser size

    m_repaintCount = 0;
    m_context->requestRepaint = [this]() {
        m_repaintCount++;
    };
}

void TestEraserToolHandler::cleanup()
{
    delete m_handler;
    delete m_context;
    delete m_layer;
    m_handler = nullptr;
    m_context = nullptr;
    m_layer = nullptr;
}

void TestEraserToolHandler::addTestStroke(const QVector<QPointF>& points)
{
    auto stroke = std::make_unique<PencilStroke>(points, Qt::red, 3);
    m_layer->addItem(std::move(stroke));
}

// ============================================================================
// Tool Identification Tests
// ============================================================================

void TestEraserToolHandler::testToolId()
{
    QCOMPARE(m_handler->toolId(), ToolId::Eraser);
}

// ============================================================================
// Capability Tests
// ============================================================================

void TestEraserToolHandler::testSupportsColor()
{
    // Eraser doesn't use color
    QVERIFY(!m_handler->supportsColor());
}

void TestEraserToolHandler::testSupportsWidth()
{
    // Eraser uses internal width for eraser size but doesn't expose it through supportsWidth
    // (width is controlled internally, not through the tool options panel)
    QVERIFY(!m_handler->supportsWidth());
}

// ============================================================================
// State Tests
// ============================================================================

void TestEraserToolHandler::testInitialState()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestEraserToolHandler::testIsDrawing_BeforePress()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestEraserToolHandler::testIsDrawing_DuringErase()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    QVERIFY(m_handler->isDrawing());
}

void TestEraserToolHandler::testIsDrawing_AfterRelease()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseRelease(m_context, QPoint(100, 100));

    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Mouse Event Tests
// ============================================================================

void TestEraserToolHandler::testOnMousePress_StartsErasing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));

    QVERIFY(m_handler->isDrawing());
}

void TestEraserToolHandler::testOnMouseMove_ContinuesErasing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    int repaintAfterPress = m_repaintCount;

    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QVERIFY(m_repaintCount >= repaintAfterPress);
}

void TestEraserToolHandler::testOnMouseRelease_StopsErasing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));
    m_handler->onMouseRelease(m_context, QPoint(150, 150));

    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Cancellation Tests
// ============================================================================

void TestEraserToolHandler::testCancelDrawing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    QVERIFY(m_handler->isDrawing());

    m_handler->cancelDrawing();

    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Eraser Functionality Tests
// ============================================================================

void TestEraserToolHandler::testErasesIntersectingItems()
{
    // Add a stroke that will be intersected
    QVector<QPointF> strokePoints = {
        QPointF(100, 100), QPointF(150, 100), QPointF(200, 100)
    };
    addTestStroke(strokePoints);

    QCOMPARE(m_layer->itemCount(), 1);

    // Erase over the stroke
    m_handler->onMousePress(m_context, QPoint(150, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 100));
    m_handler->onMouseRelease(m_context, QPoint(150, 100));

    // Eraser stores removed items in an ErasedItemsGroup for undo/redo support.
    QCOMPARE(m_layer->itemCount(), 1);
    QVERIFY(dynamic_cast<ErasedItemsGroup*>(m_layer->itemAt(0)) != nullptr);
}

void TestEraserToolHandler::testDoesNotEraseNonIntersecting()
{
    // Add a stroke far from where we'll erase
    QVector<QPointF> strokePoints = {
        QPointF(400, 400), QPointF(450, 400), QPointF(500, 400)
    };
    addTestStroke(strokePoints);

    QCOMPARE(m_layer->itemCount(), 1);

    // Erase far from the stroke
    m_context->width = 20;
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));
    m_handler->onMouseRelease(m_context, QPoint(150, 150));

    // The stroke should still exist
    QCOMPARE(m_layer->itemCount(), 1);
}

QTEST_MAIN(TestEraserToolHandler)
#include "tst_EraserToolHandler.moc"
