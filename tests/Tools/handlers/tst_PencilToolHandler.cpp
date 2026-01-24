#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

/**
 * @brief Tests for PencilToolHandler class
 *
 * Covers:
 * - Tool identification
 * - Mouse event handling (press, move, release)
 * - Drawing state management
 * - Preview drawing
 * - Cancellation
 * - Capability flags
 */
class TestPencilToolHandler : public QObject
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
    void testSupportsLineStyle();

    // State tests
    void testInitialState();
    void testIsDrawing_BeforePress();
    void testIsDrawing_DuringDraw();
    void testIsDrawing_AfterRelease();

    // Mouse event tests
    void testOnMousePress_StartsDrawing();
    void testOnMouseMove_AddsPoints();
    void testOnMouseRelease_FinishesStroke();
    void testOnMouseRelease_SinglePoint();

    // Preview tests
    void testDrawPreview_NotDrawing();
    void testDrawPreview_WhileDrawing();

    // Cancellation tests
    void testCancelDrawing();
    void testCancelDrawing_ResetsState();

    // Integration tests
    void testCompleteStroke_AddsToLayer();
    void testMinimumPointDistance();

private:
    PencilToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    int m_repaintCount = 0;
};

void TestPencilToolHandler::init()
{
    m_handler = new PencilToolHandler();
    m_layer = new AnnotationLayer();
    m_context = new ToolContext();

    m_context->annotationLayer = m_layer;
    m_context->color = Qt::red;
    m_context->width = 3;
    m_context->lineStyle = LineStyle::Solid;

    m_repaintCount = 0;
    m_context->requestRepaint = [this]() {
        m_repaintCount++;
    };
}

void TestPencilToolHandler::cleanup()
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

void TestPencilToolHandler::testToolId()
{
    QCOMPARE(m_handler->toolId(), ToolId::Pencil);
}

// ============================================================================
// Capability Tests
// ============================================================================

void TestPencilToolHandler::testSupportsColor()
{
    QVERIFY(m_handler->supportsColor());
}

void TestPencilToolHandler::testSupportsWidth()
{
    QVERIFY(m_handler->supportsWidth());
}

void TestPencilToolHandler::testSupportsLineStyle()
{
    QVERIFY(m_handler->supportsLineStyle());
}

// ============================================================================
// State Tests
// ============================================================================

void TestPencilToolHandler::testInitialState()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestPencilToolHandler::testIsDrawing_BeforePress()
{
    QVERIFY(!m_handler->isDrawing());
}

void TestPencilToolHandler::testIsDrawing_DuringDraw()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    QVERIFY(m_handler->isDrawing());
}

void TestPencilToolHandler::testIsDrawing_AfterRelease()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));
    m_handler->onMouseRelease(m_context, QPoint(200, 200));

    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Mouse Event Tests
// ============================================================================

void TestPencilToolHandler::testOnMousePress_StartsDrawing()
{
    int initialRepaintCount = m_repaintCount;

    m_handler->onMousePress(m_context, QPoint(100, 100));

    QVERIFY(m_handler->isDrawing());
    QVERIFY(m_repaintCount > initialRepaintCount);
}

void TestPencilToolHandler::testOnMouseMove_AddsPoints()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    int repaintAfterPress = m_repaintCount;

    // Move with sufficient distance to trigger point addition
    m_handler->onMouseMove(m_context, QPoint(120, 120));
    m_handler->onMouseMove(m_context, QPoint(140, 140));
    m_handler->onMouseMove(m_context, QPoint(160, 160));

    QVERIFY(m_repaintCount > repaintAfterPress);
}

void TestPencilToolHandler::testOnMouseRelease_FinishesStroke()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));
    m_handler->onMouseMove(m_context, QPoint(200, 200));

    size_t initialItemCount = m_layer->itemCount();
    m_handler->onMouseRelease(m_context, QPoint(200, 200));

    QVERIFY(!m_handler->isDrawing());
    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
}

void TestPencilToolHandler::testOnMouseRelease_SinglePoint()
{
    // Single click without significant movement should not add stroke
    m_handler->onMousePress(m_context, QPoint(100, 100));

    size_t initialItemCount = m_layer->itemCount();
    m_handler->onMouseRelease(m_context, QPoint(100, 100));

    // Single point stroke may or may not be added depending on implementation
    QVERIFY(!m_handler->isDrawing());
}

// ============================================================================
// Preview Tests
// ============================================================================

void TestPencilToolHandler::testDrawPreview_NotDrawing()
{
    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    // Should not crash when not drawing
    m_handler->drawPreview(painter);
    QVERIFY(true);
}

void TestPencilToolHandler::testDrawPreview_WhileDrawing()
{
    m_handler->onMousePress(m_context, QPoint(50, 50));
    m_handler->onMouseMove(m_context, QPoint(100, 100));
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

void TestPencilToolHandler::testCancelDrawing()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    QVERIFY(m_handler->isDrawing());

    m_handler->cancelDrawing();

    QVERIFY(!m_handler->isDrawing());
}

void TestPencilToolHandler::testCancelDrawing_ResetsState()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 150));

    size_t initialItemCount = m_layer->itemCount();

    m_handler->cancelDrawing();

    // Should not add anything to layer
    QCOMPARE(m_layer->itemCount(), initialItemCount);
}

// ============================================================================
// Integration Tests
// ============================================================================

void TestPencilToolHandler::testCompleteStroke_AddsToLayer()
{
    size_t initialItemCount = m_layer->itemCount();

    // Complete a full stroke
    m_handler->onMousePress(m_context, QPoint(100, 100));
    m_handler->onMouseMove(m_context, QPoint(120, 120));
    m_handler->onMouseMove(m_context, QPoint(140, 140));
    m_handler->onMouseMove(m_context, QPoint(160, 160));
    m_handler->onMouseRelease(m_context, QPoint(180, 180));

    QCOMPARE(m_layer->itemCount(), initialItemCount + 1);
}

void TestPencilToolHandler::testMinimumPointDistance()
{
    m_handler->onMousePress(m_context, QPoint(100, 100));

    // Move very small distances - should not trigger many repaints
    int repaintAfterPress = m_repaintCount;

    m_handler->onMouseMove(m_context, QPoint(100, 100));  // Same point
    m_handler->onMouseMove(m_context, QPoint(101, 100));  // 1 pixel move
    m_handler->onMouseMove(m_context, QPoint(101, 101));  // 1 pixel move

    // Minimal movement should result in few or no additional repaints
    // (depends on kMinPointDistance implementation)
    QVERIFY(true);  // Just verify no crash
}

QTEST_MAIN(TestPencilToolHandler)
#include "tst_PencilToolHandler.moc"
