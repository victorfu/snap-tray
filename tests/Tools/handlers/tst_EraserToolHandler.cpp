#include <QtTest/QtTest>
#include "tools/handlers/EraserToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ErasedItemsGroup.h"
#include "annotations/MarkerStroke.h"
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
    void testErasesIntersectingMarkerItems();
    void testDoesNotEraseNonIntersecting();
    void testCursor_UsesCenteredPixmapHotspot();
    void testCursor_UsesSingleCrispOutline();

private:
    EraserToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    int m_repaintCount = 0;

    void addTestStroke(const QVector<QPointF>& points);
    void addTestMarker(const QVector<QPointF>& points);
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

void TestEraserToolHandler::addTestMarker(const QVector<QPointF>& points)
{
    auto stroke = std::make_unique<MarkerStroke>(points, Qt::yellow, 20);
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

void TestEraserToolHandler::testErasesIntersectingMarkerItems()
{
    const QVector<QPointF> markerPoints = {
        QPointF(100, 100), QPointF(150, 100), QPointF(200, 100)
    };
    addTestMarker(markerPoints);

    QCOMPARE(m_layer->itemCount(), 1);

    m_handler->onMousePress(m_context, QPoint(150, 100));
    m_handler->onMouseMove(m_context, QPoint(150, 100));
    m_handler->onMouseRelease(m_context, QPoint(150, 100));

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

void TestEraserToolHandler::testCursor_UsesCenteredPixmapHotspot()
{
    const QCursor cursor = m_handler->cursor();
    const QPixmap pixmap = cursor.pixmap();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.devicePixelRatio() >= 2.0);
    QCOMPARE(cursor.hotSpot(), QPoint(qRound(logicalSize.width() / 2.0), qRound(logicalSize.height() / 2.0)));
}

void TestEraserToolHandler::testCursor_UsesSingleCrispOutline()
{
    constexpr int expectedPadding = 4;
    constexpr QColor expectedColor(0x6C, 0x5C, 0xE7);

    const QPixmap pixmap = m_handler->cursor().pixmap();
    const qreal dpr = pixmap.devicePixelRatio();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    QVERIFY(!image.isNull());

    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }

            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }

    QVERIFY(maxX >= 0);
    QVERIFY(qAbs((maxX - minX) - (maxY - minY)) <= 1);

    auto toPhysical = [dpr](qreal value) {
        return qRound(value * dpr);
    };

    const int centerX = image.width() / 2;
    const int topY = toPhysical(expectedPadding + 1.0);
    const QRgb topPixel = image.pixel(centerX, topY);

    QVERIFY(qAlpha(topPixel) > 0);
    QCOMPARE(qRed(topPixel), expectedColor.red());
    QCOMPARE(qGreen(topPixel), expectedColor.green());
    QCOMPARE(qBlue(topPixel), expectedColor.blue());
    QCOMPARE(qAlpha(image.pixel(toPhysical(logicalSize.width() / 2.0), toPhysical(expectedPadding + 4.0))), 0);
    QCOMPARE(qAlpha(image.pixel(image.width() / 2, image.height() / 2)), 0);
}

QTEST_MAIN(TestEraserToolHandler)
#include "tst_EraserToolHandler.moc"
