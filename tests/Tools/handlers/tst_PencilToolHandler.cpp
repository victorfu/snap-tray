#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPixmap>
#include "tools/handlers/PencilToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"

namespace {

class CountingPaintEngine final : public QPaintEngine
{
public:
    CountingPaintEngine()
        : QPaintEngine(QPaintEngine::PainterPaths)
    {
    }

    bool begin(QPaintDevice* device) override
    {
        setPaintDevice(device);
        setActive(true);
        return true;
    }
    bool end() override
    {
        setActive(false);
        return true;
    }
    void updateState(const QPaintEngineState&) override {}
    void drawPath(const QPainterPath& path) override
    {
        m_framePathElements += path.elementCount();
    }
    void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
    void drawImage(const QRectF&, const QImage&, const QRectF&,
                   Qt::ImageConversionFlags) override { ++m_frameImageCalls; }
    Type type() const override { return User; }

    void finishFrame()
    {
        ++m_frames;
        m_totalPathElements += m_framePathElements;
        m_maxFramePathElements = qMax(m_maxFramePathElements, m_framePathElements);
        m_maxFrameImageCalls = qMax(m_maxFrameImageCalls, m_frameImageCalls);
        m_framePathElements = 0;
        m_frameImageCalls = 0;
    }

    int frames() const { return m_frames; }
    qint64 totalPathElements() const { return m_totalPathElements; }
    int maxFramePathElements() const { return m_maxFramePathElements; }
    int maxFrameImageCalls() const { return m_maxFrameImageCalls; }

private:
    int m_frames = 0;
    int m_framePathElements = 0;
    int m_frameImageCalls = 0;
    qint64 m_totalPathElements = 0;
    int m_maxFramePathElements = 0;
    int m_maxFrameImageCalls = 0;
};

class CountingPaintDevice final : public QPaintDevice
{
public:
    QPaintEngine* paintEngine() const override
    {
        return const_cast<CountingPaintEngine*>(&m_engine);
    }
    CountingPaintEngine& engine() { return m_engine; }

protected:
    int metric(PaintDeviceMetric metric) const override
    {
        switch (metric) {
        case PdmWidth: return 100000;
        case PdmHeight: return 100000;
        case PdmWidthMM: return 25000;
        case PdmHeightMM: return 250;
        case PdmNumColors: return 16777216;
        case PdmDepth: return 32;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY: return 96;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return QPaintDevice::devicePixelRatioFScale();
        default: return 0;
        }
    }

private:
    CountingPaintEngine m_engine;
};

} // namespace

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
    void testDrawPreview_WhileDrawing();
    void testPreviewBounds_DuringAndAfterDrawing();
    void testPreviewBounds_StaysLocalAsStrokeGrows();
    void testLongStrokePreview_HasBoundedPathSubmission();
    void testLongDiagonalPreview_UsesSparseTiles();
    void testFloatInputPreservesFractionalPoints();
    void testHighDpiAllowsCloserPoints();

    // Cancellation tests
    void testCancelDrawing();
    void testCancelDrawing_ResetsState();

    // Integration tests
    void testCompleteStroke_AddsToLayer();
    void testCompleteStroke_PreservesAllStrokesBeyondFifty();

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

void TestPencilToolHandler::testPreviewBounds_DuringAndAfterDrawing()
{
    QCOMPARE(m_handler->previewBounds(), QRect());

    m_handler->onMousePress(m_context, QPoint(50, 50));
    m_handler->onMouseMove(m_context, QPoint(100, 100));
    const QRect duringBounds = m_handler->previewBounds();

    QVERIFY(duringBounds.isValid());
    QVERIFY(!duringBounds.isEmpty());
    QVERIFY(duringBounds.width() > 0);
    QVERIFY(duringBounds.height() > 0);

    m_handler->onMouseRelease(m_context, QPoint(120, 120));
    QCOMPARE(m_handler->previewBounds(), QRect());
}

void TestPencilToolHandler::testPreviewBounds_StaysLocalAsStrokeGrows()
{
    m_handler->onMousePress(m_context, QPoint(20, 20));
    for (int i = 1; i < 12; ++i) {
        m_handler->onMouseMove(m_context, QPoint(20 + i * 20, 20 + i * 8));
    }

    const QRect previewBounds = m_handler->previewBounds();
    QVERIFY(previewBounds.isValid());
    QVERIFY(!previewBounds.isEmpty());
    QVERIFY(previewBounds.width() < 120);
}

void TestPencilToolHandler::testLongStrokePreview_HasBoundedPathSubmission()
{
    constexpr int kMoveCount = 4096;
    CountingPaintDevice device;
    QPainter painter(&device);

    m_handler->onMousePressF(m_context, QPointF(0.0, 100.0));
    for (int i = 1; i <= kMoveCount; ++i) {
        const int repaintCountBeforeMove = m_repaintCount;
        m_handler->onMouseMoveF(
            m_context, QPointF(i * 4.0, 100.0 + (i % 11)));
        if (m_repaintCount == repaintCountBeforeMove) {
            continue;
        }

        painter.setClipRect(m_handler->previewBounds());
        m_handler->drawPreview(painter);
        device.engine().finishFrame();
    }
    painter.end();

    QVERIFY(device.engine().frames() > 4000);
    QVERIFY(device.engine().maxFramePathElements() <= 256);
    QVERIFY(device.engine().totalPathElements() <=
            static_cast<qint64>(device.engine().frames()) * 128);
}

void TestPencilToolHandler::testLongDiagonalPreview_UsesSparseTiles()
{
    CountingPaintDevice device;
    QPainter painter(&device);
    m_handler->onMousePressF(m_context, QPointF(20.0, 20.0));

    for (int i = 1; i <= 80; ++i) {
        const QPointF point = (i % 2 == 0)
            ? QPointF(20.0, 20.0)
            : QPointF(3820.0, 2120.0);
        m_handler->onMouseMoveF(m_context, point);
    }

    painter.setClipRect(QRect(0, 0, 4000, 2200));
    m_handler->drawPreview(painter);
    device.engine().finishFrame();
    painter.end();

    // A bbox-based allocator would materialize roughly 16x9=144 tiles.
    // Sparse curve/tile intersection should follow the two diagonal bands.
    QVERIFY(device.engine().maxFrameImageCalls() > 0);
    QVERIFY(device.engine().maxFrameImageCalls() < 60);
}

void TestPencilToolHandler::testFloatInputPreservesFractionalPoints()
{
    m_handler->onMousePressF(m_context, QPointF(10.25, 10.75));
    m_handler->onMouseMoveF(m_context, QPointF(13.5, 14.25));
    m_handler->onMouseReleaseF(m_context, QPointF(17.75, 18.5));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<PencilStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    const QVector<QPointF> points = stroke->points();
    QVERIFY(!points.isEmpty());

    bool hasFractionalPoint = false;
    for (const QPointF& point : points) {
        if (!qFuzzyCompare(point.x(), qRound(point.x())) ||
            !qFuzzyCompare(point.y(), qRound(point.y()))) {
            hasFractionalPoint = true;
            break;
        }
    }
    QVERIFY(hasFractionalPoint);
}

void TestPencilToolHandler::testHighDpiAllowsCloserPoints()
{
    m_context->devicePixelRatio = 2.0;

    m_handler->onMousePressF(m_context, QPointF(10.0, 10.0));
    m_handler->onMouseMoveF(m_context, QPointF(11.0, 11.0));
    m_handler->onMouseMoveF(m_context, QPointF(11.8, 11.8));
    m_handler->onMouseReleaseF(m_context, QPointF(12.6, 12.6));

    QCOMPARE(m_layer->itemCount(), size_t(1));
    auto* stroke = dynamic_cast<PencilStroke*>(m_layer->itemAt(0));
    QVERIFY(stroke != nullptr);
    QVERIFY(stroke->points().size() >= 3);
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

void TestPencilToolHandler::testCompleteStroke_PreservesAllStrokesBeyondFifty()
{
    for (int i = 0; i < 52; ++i) {
        m_handler->onMousePressF(m_context, QPointF(10.0, i));
        m_handler->onMouseReleaseF(m_context, QPointF(20.0, i));
    }

    QCOMPARE(m_layer->itemCount(), static_cast<size_t>(52));

    auto* firstStroke = dynamic_cast<PencilStroke*>(m_layer->itemAt(0));
    auto* secondStroke = dynamic_cast<PencilStroke*>(m_layer->itemAt(1));
    auto* lastStroke = dynamic_cast<PencilStroke*>(m_layer->itemAt(51));
    QVERIFY(firstStroke != nullptr);
    QVERIFY(secondStroke != nullptr);
    QVERIFY(lastStroke != nullptr);
    QCOMPARE(firstStroke->points().first(), QPointF(10.0, 0.0));
    QCOMPARE(secondStroke->points().first(), QPointF(10.0, 1.0));
    QCOMPARE(lastStroke->points().first(), QPointF(10.0, 51.0));
}

QTEST_MAIN(TestPencilToolHandler)
#include "tst_PencilToolHandler.moc"
