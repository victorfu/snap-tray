#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QImage>
#include <QColor>
#include <memory>

#include "annotations/AnnotationItem.h"
#include "annotations/AnnotationLayer.h"
#include "pinwindow/RegionLayoutManager.h"

namespace {

class TrackingAnnotation final : public AnnotationItem
{
public:
    explicit TrackingAnnotation(const QRect& rect)
        : m_rect(rect)
    {
    }

    void draw(QPainter& painter) const override { Q_UNUSED(painter) }
    QRect boundingRect() const override { return m_rect; }
    std::unique_ptr<AnnotationItem> clone() const override
    {
        return std::make_unique<TrackingAnnotation>(m_rect);
    }
    void translate(const QPointF& delta) override
    {
        m_rect.translate(qRound(delta.x()), qRound(delta.y()));
    }

private:
    QRect m_rect;
};

TrackingAnnotation* addTrackingAnnotation(AnnotationLayer& layer, const QRect& rect)
{
    auto annotation = std::make_unique<TrackingAnnotation>(rect);
    auto* result = annotation.get();
    layer.addItem(std::move(annotation));
    return result;
}

}  // namespace

class TestRegionLayoutManager : public QObject
{
    Q_OBJECT

private:
    QVector<LayoutRegion> createTestRegions(int count = 3) {
        QVector<LayoutRegion> regions;
        QColor colors[] = {Qt::blue, Qt::green, Qt::red, Qt::yellow, Qt::cyan};

        for (int i = 0; i < count; ++i) {
            LayoutRegion region;
            region.rect = QRect(i * 110, 0, 100, 100);
            region.originalRect = region.rect;
            region.image = QImage(100, 100, QImage::Format_ARGB32);
            region.image.fill(colors[i % 5]);
            region.color = colors[i % 5];
            region.index = i + 1;
            region.isSelected = false;
            regions.append(region);
        }
        return regions;
    }

    static LayoutRegion createRegion(const QRect& rect) {
        LayoutRegion region;
        region.rect = rect;
        region.originalRect = rect;
        region.image = QImage(rect.size(), QImage::Format_ARGB32);
        region.image.fill(Qt::blue);
        region.index = 1;
        return region;
    }

    static QPoint handlePoint(const QRect& rect, ResizeHandler::Edge edge) {
        switch (edge) {
            case ResizeHandler::Edge::TopLeft: return rect.topLeft();
            case ResizeHandler::Edge::Top: return QPoint(rect.center().x(), rect.top());
            case ResizeHandler::Edge::TopRight: return rect.topRight();
            case ResizeHandler::Edge::Right: return QPoint(rect.right(), rect.center().y());
            case ResizeHandler::Edge::BottomRight: return rect.bottomRight();
            case ResizeHandler::Edge::Bottom: return QPoint(rect.center().x(), rect.bottom());
            case ResizeHandler::Edge::BottomLeft: return rect.bottomLeft();
            case ResizeHandler::Edge::Left: return QPoint(rect.left(), rect.center().y());
            default: return {};
        }
    }

    static void verifyResizeAnchor(const QRect& start,
                                   const QRect& result,
                                   ResizeHandler::Edge edge) {
        const int startRight = start.x() + start.width();
        const int startBottom = start.y() + start.height();
        const int resultRight = result.x() + result.width();
        const int resultBottom = result.y() + result.height();
        const int startCenterX2 = start.x() * 2 + start.width();
        const int startCenterY2 = start.y() * 2 + start.height();
        const int resultCenterX2 = result.x() * 2 + result.width();
        const int resultCenterY2 = result.y() * 2 + result.height();

        switch (edge) {
            case ResizeHandler::Edge::TopLeft:
                QCOMPARE(resultRight, startRight);
                QCOMPARE(resultBottom, startBottom);
                break;
            case ResizeHandler::Edge::Top:
                QCOMPARE(resultBottom, startBottom);
                QVERIFY(qAbs(resultCenterX2 - startCenterX2) <= 1);
                break;
            case ResizeHandler::Edge::TopRight:
                QCOMPARE(result.x(), start.x());
                QCOMPARE(resultBottom, startBottom);
                break;
            case ResizeHandler::Edge::Right:
                QCOMPARE(result.x(), start.x());
                QVERIFY(qAbs(resultCenterY2 - startCenterY2) <= 1);
                break;
            case ResizeHandler::Edge::BottomRight:
                QCOMPARE(result.x(), start.x());
                QCOMPARE(result.y(), start.y());
                break;
            case ResizeHandler::Edge::Bottom:
                QCOMPARE(result.y(), start.y());
                QVERIFY(qAbs(resultCenterX2 - startCenterX2) <= 1);
                break;
            case ResizeHandler::Edge::BottomLeft:
                QCOMPARE(resultRight, startRight);
                QCOMPARE(result.y(), start.y());
                break;
            case ResizeHandler::Edge::Left:
                QCOMPARE(resultRight, startRight);
                QVERIFY(qAbs(resultCenterY2 - startCenterY2) <= 1);
                break;
            default:
                QFAIL("Unexpected resize edge");
        }
    }

    static void verifyAspectAndBounds(const QRect& start, const QRect& result) {
        QVERIFY(result.width() >= LayoutModeConstants::kMinRegionSize);
        QVERIFY(result.height() >= LayoutModeConstants::kMinRegionSize);
        QVERIFY(result.left() >= 0);
        QVERIFY(result.top() >= 0);
        QVERIFY(result.right() < LayoutModeConstants::kMaxCanvasSize);
        QVERIFY(result.bottom() < LayoutModeConstants::kMaxCanvasSize);

        const qint64 crossError = qAbs(
            static_cast<qint64>(result.width()) * start.height() -
            static_cast<qint64>(result.height()) * start.width());
        QVERIFY(crossError <= start.width() + start.height());
    }

private slots:
    // =========================================================================
    // Mode Control Tests
    // =========================================================================

    void testInitialState() {
        RegionLayoutManager manager;
        QVERIFY(!manager.isActive());
        QCOMPARE(manager.selectedIndex(), -1);
        QVERIFY(manager.regions().isEmpty());
    }

    void testEnterLayoutMode() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(3);
        QSize canvasSize(330, 100);

        QSignalSpy layoutSpy(&manager, &RegionLayoutManager::layoutChanged);

        manager.enterLayoutMode(regions, canvasSize);

        QVERIFY(manager.isActive());
        QCOMPARE(manager.regions().size(), 3);
        QCOMPARE(manager.canvasBounds().size(), canvasSize);
        QCOMPARE(layoutSpy.count(), 1);
    }

    void testExitLayoutModeApply() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);

        manager.enterLayoutMode(regions, QSize(220, 100));
        QVERIFY(manager.isActive());

        manager.exitLayoutMode(true);
        QVERIFY(!manager.isActive());
    }

    void testExitLayoutModeCancel() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);

        manager.enterLayoutMode(regions, QSize(220, 100));

        // Move a region
        manager.selectRegion(0);
        manager.startDrag(QPoint(50, 50));
        manager.updateDrag(QPoint(100, 100));
        manager.finishDrag();

        // Cancel should restore original positions
        manager.exitLayoutMode(false);
        QVERIFY(!manager.isActive());
    }

    // =========================================================================
    // Hit Testing Tests
    // =========================================================================

    void testHitTestRegion() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(3);
        manager.enterLayoutMode(regions, QSize(330, 100));

        // Hit first region
        QCOMPARE(manager.hitTestRegion(QPoint(50, 50)), 0);

        // Hit second region
        QCOMPARE(manager.hitTestRegion(QPoint(160, 50)), 1);

        // Hit third region
        QCOMPARE(manager.hitTestRegion(QPoint(270, 50)), 2);

        // Miss (between regions)
        QCOMPARE(manager.hitTestRegion(QPoint(105, 50)), -1);

        // Miss (outside all regions)
        QCOMPARE(manager.hitTestRegion(QPoint(400, 50)), -1);
    }

    void testHitTestHandle() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(1);
        manager.enterLayoutMode(regions, QSize(100, 100));

        // No region selected - should return None
        QCOMPARE(manager.hitTestHandle(QPoint(0, 0)), ResizeHandler::Edge::None);

        // Select a region
        manager.selectRegion(0);

        // Test corner handles
        QCOMPARE(manager.hitTestHandle(QPoint(0, 0)), ResizeHandler::Edge::TopLeft);
        QCOMPARE(manager.hitTestHandle(QPoint(99, 0)), ResizeHandler::Edge::TopRight);
        QCOMPARE(manager.hitTestHandle(QPoint(0, 99)), ResizeHandler::Edge::BottomLeft);
        QCOMPARE(manager.hitTestHandle(QPoint(99, 99)), ResizeHandler::Edge::BottomRight);

        // Test edge handles
        QCOMPARE(manager.hitTestHandle(QPoint(50, 0)), ResizeHandler::Edge::Top);
        QCOMPARE(manager.hitTestHandle(QPoint(50, 99)), ResizeHandler::Edge::Bottom);
        QCOMPARE(manager.hitTestHandle(QPoint(0, 50)), ResizeHandler::Edge::Left);
        QCOMPARE(manager.hitTestHandle(QPoint(99, 50)), ResizeHandler::Edge::Right);

        // Center should not hit any handle
        QCOMPARE(manager.hitTestHandle(QPoint(50, 50)), ResizeHandler::Edge::None);
    }

    // =========================================================================
    // Selection Tests
    // =========================================================================

    void testSelectRegion() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(3);
        manager.enterLayoutMode(regions, QSize(330, 100));

        QSignalSpy selectionSpy(&manager, &RegionLayoutManager::selectionChanged);

        manager.selectRegion(1);
        QCOMPARE(manager.selectedIndex(), 1);
        QCOMPARE(selectionSpy.count(), 1);
        QCOMPARE(selectionSpy.first().first().toInt(), 1);

        // Select another
        manager.selectRegion(2);
        QCOMPARE(manager.selectedIndex(), 2);

        // Deselect
        manager.selectRegion(-1);
        QCOMPARE(manager.selectedIndex(), -1);
    }

    void testSelectRegionOutOfBounds() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        manager.enterLayoutMode(regions, QSize(220, 100));

        // Invalid index should be clamped to -1
        manager.selectRegion(10);
        QCOMPARE(manager.selectedIndex(), -1);

        manager.selectRegion(-5);
        QCOMPARE(manager.selectedIndex(), -1);
    }

    // =========================================================================
    // Drag Tests
    // =========================================================================

    void testDragRegion() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(1);
        manager.enterLayoutMode(regions, QSize(100, 100));

        QRect originalRect = manager.regions()[0].rect;

        manager.selectRegion(0);
        manager.startDrag(QPoint(50, 50));
        QVERIFY(manager.isDragging());

        manager.updateDrag(QPoint(100, 100));
        QRect newRect = manager.regions()[0].rect;
        QCOMPARE(newRect.topLeft(), originalRect.topLeft() + QPoint(50, 50));

        manager.finishDrag();
        QVERIFY(!manager.isDragging());
    }

    void testDragClampUsesMaximumCanvasExtent() {
        RegionLayoutManager manager;
        const QRect startRect(0, 0, 100, 100);
        manager.enterLayoutMode({createRegion(startRect)}, startRect.size());
        manager.selectRegion(0);
        manager.startDrag(startRect.center());
        manager.updateDrag(QPoint(20000, 20000));
        manager.finishDrag();

        const QRect result = manager.regions()[0].rect;
        QCOMPARE(result.right(), LayoutModeConstants::kMaxCanvasSize - 1);
        QCOMPARE(result.bottom(), LayoutModeConstants::kMaxCanvasSize - 1);
        QCOMPARE(manager.canvasBounds().size(), QSize(
            LayoutModeConstants::kMaxCanvasSize,
            LayoutModeConstants::kMaxCanvasSize));
    }

    // =========================================================================
    // Resize Tests
    // =========================================================================

    void testResizeEnforcesMinimumSize() {
        RegionLayoutManager manager;

        LayoutRegion region;
        region.rect = QRect(0, 0, 100, 100);
        region.originalRect = region.rect;
        region.image = QImage(100, 100, QImage::Format_ARGB32);
        region.color = Qt::blue;
        region.index = 1;

        QVector<LayoutRegion> regions;
        regions.append(region);

        manager.enterLayoutMode(regions, QSize(100, 100));
        manager.selectRegion(0);

        // Try to resize to smaller than minimum
        manager.startResize(ResizeHandler::Edge::BottomRight, QPoint(99, 99));
        manager.updateResize(QPoint(10, 10), false);
        manager.finishResize();

        QRect finalRect = manager.regions()[0].rect;
        QVERIFY(finalRect.width() >= LayoutModeConstants::kMinRegionSize);
        QVERIFY(finalRect.height() >= LayoutModeConstants::kMinRegionSize);
    }

    void testResizeClampKeepsRegionInsideCanvasAfterMoveRight() {
        RegionLayoutManager manager;

        LayoutRegion region;
        region.rect = QRect(1000, 0, 100, 100);
        region.originalRect = region.rect;
        region.image = QImage(100, 100, QImage::Format_ARGB32);
        region.color = Qt::blue;
        region.index = 1;

        QVector<LayoutRegion> regions;
        regions.append(region);

        manager.enterLayoutMode(regions, QSize(1100, 100));
        manager.selectRegion(0);

        manager.startResize(ResizeHandler::Edge::Top, QPoint(1050, 0));
        manager.updateResize(QPoint(1050, -10000), true);
        manager.finishResize();

        const QRect finalRect = manager.regions()[0].rect;
        QVERIFY(finalRect.left() >= 0);
        QVERIFY(finalRect.top() >= 0);
        QVERIFY(finalRect.right() < LayoutModeConstants::kMaxCanvasSize);
        QVERIFY(finalRect.bottom() < LayoutModeConstants::kMaxCanvasSize);
    }

    void testAspectResizeMaintainsMinimum_data() {
        QTest::addColumn<QRect>("startRect");
        QTest::addColumn<int>("edgeValue");
        QTest::addColumn<QPoint>("delta");

        const QRect wide(100, 100, 1000, 50);
        QTest::newRow("wide-left") << wide << static_cast<int>(ResizeHandler::Edge::Left) << QPoint(950, 0);
        QTest::newRow("wide-right") << wide << static_cast<int>(ResizeHandler::Edge::Right) << QPoint(-950, 0);
        QTest::newRow("wide-top-left") << wide << static_cast<int>(ResizeHandler::Edge::TopLeft) << QPoint(950, 0);
        QTest::newRow("wide-top-right") << wide << static_cast<int>(ResizeHandler::Edge::TopRight) << QPoint(-950, 0);
        QTest::newRow("wide-bottom-left") << wide << static_cast<int>(ResizeHandler::Edge::BottomLeft) << QPoint(950, 0);
        QTest::newRow("wide-bottom-right") << wide << static_cast<int>(ResizeHandler::Edge::BottomRight) << QPoint(-950, 0);

        const QRect tall(100, 100, 50, 1000);
        QTest::newRow("tall-top") << tall << static_cast<int>(ResizeHandler::Edge::Top) << QPoint(0, 950);
        QTest::newRow("tall-bottom") << tall << static_cast<int>(ResizeHandler::Edge::Bottom) << QPoint(0, -950);
        QTest::newRow("tall-top-left") << tall << static_cast<int>(ResizeHandler::Edge::TopLeft) << QPoint(0, 950);
        QTest::newRow("tall-top-right") << tall << static_cast<int>(ResizeHandler::Edge::TopRight) << QPoint(0, 950);
        QTest::newRow("tall-bottom-left") << tall << static_cast<int>(ResizeHandler::Edge::BottomLeft) << QPoint(0, -950);
        QTest::newRow("tall-bottom-right") << tall << static_cast<int>(ResizeHandler::Edge::BottomRight) << QPoint(0, -950);
    }

    void testAspectResizeMaintainsMinimum() {
        QFETCH(QRect, startRect);
        QFETCH(int, edgeValue);
        QFETCH(QPoint, delta);
        const auto edge = static_cast<ResizeHandler::Edge>(edgeValue);

        RegionLayoutManager manager;
        manager.enterLayoutMode({createRegion(startRect)}, QSize(1200, 1200));
        manager.selectRegion(0);
        const QPoint startPos = handlePoint(startRect, edge);
        manager.startResize(edge, startPos);
        manager.updateResize(startPos + delta, true);

        const QRect result = manager.regions()[0].rect;
        verifyAspectAndBounds(startRect, result);
        verifyResizeAnchor(startRect, result, edge);
    }

    void testResizeMinimumIsExactlyFiftyWithoutAspect_data() {
        QTest::addColumn<int>("edgeValue");
        QTest::addColumn<QPoint>("delta");

        QTest::newRow("top-left") << static_cast<int>(ResizeHandler::Edge::TopLeft) << QPoint(99, 99);
        QTest::newRow("top") << static_cast<int>(ResizeHandler::Edge::Top) << QPoint(0, 99);
        QTest::newRow("top-right") << static_cast<int>(ResizeHandler::Edge::TopRight) << QPoint(-99, 99);
        QTest::newRow("right") << static_cast<int>(ResizeHandler::Edge::Right) << QPoint(-99, 0);
        QTest::newRow("bottom-right") << static_cast<int>(ResizeHandler::Edge::BottomRight) << QPoint(-99, -99);
        QTest::newRow("bottom") << static_cast<int>(ResizeHandler::Edge::Bottom) << QPoint(0, -99);
        QTest::newRow("bottom-left") << static_cast<int>(ResizeHandler::Edge::BottomLeft) << QPoint(99, -99);
        QTest::newRow("left") << static_cast<int>(ResizeHandler::Edge::Left) << QPoint(99, 0);
    }

    void testResizeMinimumIsExactlyFiftyWithoutAspect() {
        QFETCH(int, edgeValue);
        QFETCH(QPoint, delta);
        const auto edge = static_cast<ResizeHandler::Edge>(edgeValue);
        const QRect startRect(100, 100, 100, 100);

        RegionLayoutManager manager;
        manager.enterLayoutMode({createRegion(startRect)}, QSize(300, 300));
        manager.selectRegion(0);
        const QPoint startPos = handlePoint(startRect, edge);
        manager.startResize(edge, startPos);
        manager.updateResize(startPos + delta, false);

        const QRect result = manager.regions()[0].rect;
        if (edge != ResizeHandler::Edge::Top && edge != ResizeHandler::Edge::Bottom) {
            QCOMPARE(result.width(), LayoutModeConstants::kMinRegionSize);
        }
        if (edge != ResizeHandler::Edge::Left && edge != ResizeHandler::Edge::Right) {
            QCOMPARE(result.height(), LayoutModeConstants::kMinRegionSize);
        }
        verifyResizeAnchor(startRect, result, edge);
    }

    void testAspectResizeRespectsCanvasAndAnchor_data() {
        QTest::addColumn<int>("edgeValue");
        QTest::addColumn<QPoint>("delta");

        QTest::newRow("top-left") << static_cast<int>(ResizeHandler::Edge::TopLeft) << QPoint(-10000, -10000);
        QTest::newRow("top") << static_cast<int>(ResizeHandler::Edge::Top) << QPoint(0, -10000);
        QTest::newRow("top-right") << static_cast<int>(ResizeHandler::Edge::TopRight) << QPoint(10000, -10000);
        QTest::newRow("right") << static_cast<int>(ResizeHandler::Edge::Right) << QPoint(10000, 0);
        QTest::newRow("bottom-right") << static_cast<int>(ResizeHandler::Edge::BottomRight) << QPoint(10000, 10000);
        QTest::newRow("bottom") << static_cast<int>(ResizeHandler::Edge::Bottom) << QPoint(0, 10000);
        QTest::newRow("bottom-left") << static_cast<int>(ResizeHandler::Edge::BottomLeft) << QPoint(-10000, 10000);
        QTest::newRow("left") << static_cast<int>(ResizeHandler::Edge::Left) << QPoint(-10000, 0);
    }

    void testAspectResizeRespectsCanvasAndAnchor() {
        QFETCH(int, edgeValue);
        QFETCH(QPoint, delta);
        const auto edge = static_cast<ResizeHandler::Edge>(edgeValue);
        const QRect startRect(4900, 4900, 101, 67);

        RegionLayoutManager manager;
        manager.enterLayoutMode({createRegion(startRect)}, QSize(5001, 4967));
        manager.selectRegion(0);
        const QPoint startPos = handlePoint(startRect, edge);
        const QPoint requestedPos = startPos + delta;
        manager.startResize(edge, startPos);
        manager.updateResize(requestedPos, true);
        const QRect firstResult = manager.regions()[0].rect;
        manager.updateResize(requestedPos, true);
        const QRect secondResult = manager.regions()[0].rect;

        QCOMPARE(secondResult, firstResult);
        verifyAspectAndBounds(startRect, firstResult);
        verifyResizeAnchor(startRect, firstResult, edge);
    }

    void testAspectResizeCanReachJointMinimumAndToggle() {
        const QRect startRect(100, 100, 100, 200);
        const auto edge = ResizeHandler::Edge::Right;
        const QPoint startPos = handlePoint(startRect, edge);
        const QPoint requestedPos = startPos + QPoint(-90, 0);

        RegionLayoutManager manager;
        manager.enterLayoutMode({createRegion(startRect)}, QSize(300, 400));
        manager.selectRegion(0);
        manager.startResize(edge, startPos);

        manager.updateResize(requestedPos, false);
        QCOMPARE(manager.regions()[0].rect.size(), QSize(50, 200));

        manager.updateResize(requestedPos, true);
        const QRect aspectResult = manager.regions()[0].rect;
        QCOMPARE(aspectResult.size(), QSize(50, 100));
        verifyResizeAnchor(startRect, aspectResult, edge);

        manager.updateResize(requestedPos, false);
        QCOMPARE(manager.regions()[0].rect.size(), QSize(50, 200));
    }

    void testResizeRaisesImportedSmallRegionToMinimum() {
        auto resize = [](const QRect& startRect,
                         ResizeHandler::Edge edge,
                         const QPoint& delta,
                         bool maintainAspectRatio) {
            RegionLayoutManager manager;
            manager.enterLayoutMode({createRegion(startRect)}, QSize(
                LayoutModeConstants::kMaxCanvasSize,
                LayoutModeConstants::kMaxCanvasSize));
            manager.selectRegion(0);
            const QPoint startPos = handlePoint(startRect, edge);
            manager.startResize(edge, startPos);
            manager.updateResize(startPos + delta, maintainAspectRatio);
            return manager.regions()[0].rect;
        };

        const QRect smallAtOrigin(0, 0, 20, 20);
        QCOMPARE(resize(smallAtOrigin, ResizeHandler::Edge::Right,
                        QPoint(80, 0), false).size(),
                 QSize(100, 50));
        QCOMPARE(resize(smallAtOrigin, ResizeHandler::Edge::Top,
                        QPoint(0, -80), false).size(),
                 QSize(50, 100));
        QCOMPARE(resize(smallAtOrigin, ResizeHandler::Edge::Right,
                        QPoint(80, 0), true).size(),
                 QSize(100, 100));

        const QRect smallAtBottomRight(
            LayoutModeConstants::kMaxCanvasSize - 20,
            LayoutModeConstants::kMaxCanvasSize - 20,
            20,
            20);
        const QRect boundaryResult = resize(
            smallAtBottomRight,
            ResizeHandler::Edge::Right,
            QPoint(80, 0),
            false);
        QCOMPARE(boundaryResult.size(), QSize(100, 50));
        QCOMPARE(boundaryResult.right(), LayoutModeConstants::kMaxCanvasSize - 1);
        QCOMPARE(boundaryResult.bottom(), LayoutModeConstants::kMaxCanvasSize - 1);

        const QRect narrow(0, 0, 20, 100);
        const QRect narrowResult = resize(
            narrow, ResizeHandler::Edge::Right, QPoint(80, 0), true);
        verifyAspectAndBounds(narrow, narrowResult);
        QCOMPARE(narrowResult.size(), QSize(100, 500));
        QCOMPARE(resize(QRect(0, 0, 10, 100),
                        ResizeHandler::Edge::Right, QPoint(), true).size(),
                 QSize(50, 500));

        const QRect shortRegion(0, 0, 100, 20);
        const QRect shortResult = resize(
            shortRegion, ResizeHandler::Edge::Bottom, QPoint(0, 80), true);
        verifyAspectAndBounds(shortRegion, shortResult);
        QCOMPARE(shortResult.size(), QSize(500, 100));
        QCOMPARE(resize(QRect(0, 0, 100, 10),
                        ResizeHandler::Edge::Bottom, QPoint(), true).size(),
                 QSize(500, 50));

        const QRect impossibleWide(0, 0, LayoutModeConstants::kMaxCanvasSize, 1);
        const QRect impossibleWideResult = resize(
            impossibleWide, ResizeHandler::Edge::Right, QPoint(), true);
        QCOMPARE(impossibleWideResult.size(), QSize(
            LayoutModeConstants::kMaxCanvasSize,
            LayoutModeConstants::kMinRegionSize));
        QVERIFY(impossibleWideResult.bottom() < LayoutModeConstants::kMaxCanvasSize);

        const QRect impossibleTall(0, 0, 1, LayoutModeConstants::kMaxCanvasSize);
        const QRect impossibleTallResult = resize(
            impossibleTall, ResizeHandler::Edge::Bottom, QPoint(), true);
        QCOMPARE(impossibleTallResult.size(), QSize(
            LayoutModeConstants::kMinRegionSize,
            LayoutModeConstants::kMaxCanvasSize));
        QVERIFY(impossibleTallResult.right() < LayoutModeConstants::kMaxCanvasSize);

        QCOMPARE(resize(QRect(0, 0, 10, 2160),
                        ResizeHandler::Edge::Right, QPoint(), true).size(),
                 QSize(50, 2160));
        QCOMPARE(resize(QRect(0, 0, 2160, 10),
                        ResizeHandler::Edge::Bottom, QPoint(), true).size(),
                 QSize(2160, 50));
    }

    // =========================================================================
    // Canvas Bounds Tests
    // =========================================================================

    void testCanvasBoundsExpand() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(1);
        manager.enterLayoutMode(regions, QSize(100, 100));

        QSignalSpy canvasSpy(&manager, &RegionLayoutManager::canvasSizeChanged);

        manager.selectRegion(0);
        manager.startDrag(QPoint(50, 50));
        manager.updateDrag(QPoint(150, 150));  // Drag 100 pixels right and down
        manager.finishDrag();

        // Canvas should have expanded
        QRect bounds = manager.canvasBounds();
        QVERIFY(bounds.width() > 100 || bounds.height() > 100);
    }

    // =========================================================================
    // Annotation Integration Tests
    // =========================================================================

    void testAnnotationsFollowTheirRegionsAndFinalCanvasOrigin() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        AnnotationLayer layer;
        auto* first = addTrackingAnnotation(layer, QRect(20, 20, 10, 10));
        auto* second = addTrackingAnnotation(layer, QRect(140, 20, 10, 10));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(0);
        manager.startDrag(QPoint(50, 50));
        manager.updateDrag(QPoint(100, 80));
        manager.finishDrag();

        QSignalSpy changedSpy(&layer, &AnnotationLayer::changed);
        const auto revisionBefore = layer.revision();
        manager.updateAnnotationPositions(&layer);

        // The final canvas starts at x=50 because the untouched second region
        // is now the leftmost region. Each annotation follows its own region,
        // then both are normalized to that final origin.
        QCOMPARE(first->boundingRect(), QRect(20, 50, 10, 10));
        QCOMPARE(second->boundingRect(), QRect(90, 20, 10, 10));
        QCOMPARE(changedSpy.count(), 1);
        QVERIFY(layer.revision() > revisionBefore);
    }

    void testAnnotationCenterTracksRegionResizeWithoutScalingGeometry() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        AnnotationLayer layer;
        auto* annotation = addTrackingAnnotation(layer, QRect(130, 20, 10, 10));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(1);
        manager.startResize(ResizeHandler::Edge::BottomRight, QPoint(209, 99));
        manager.updateResize(QPoint(309, 199), false);
        manager.finishResize();
        manager.updateAnnotationPositions(&layer);

        // The original center is 25% across/down the region. It moves to the
        // same relative point in the 200x200 region, while its own size stays
        // 10x10 because AnnotationItem has translation, not affine scaling.
        QCOMPARE(annotation->boundingRect(), QRect(155, 45, 10, 10));
    }

    void testAnnotationInGapIsNotAttachedToNearestRegion() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        AnnotationLayer layer;
        auto* annotation = addTrackingAnnotation(layer, QRect(101, 40, 8, 8));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(1);
        manager.startDrag(QPoint(160, 50));
        manager.updateDrag(QPoint(210, 50));
        manager.finishDrag();
        manager.updateAnnotationPositions(&layer);

        QCOMPARE(annotation->boundingRect(), QRect(101, 40, 8, 8));
    }

    void testAnnotationPreviewKeepsCanvasOriginUntilRecompose() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        TrackingAnnotation annotation(QRect(20, 20, 10, 10));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(0);
        manager.startDrag(QPoint(50, 50));
        manager.updateDrag(QPoint(100, 50));
        manager.finishDrag();
        manager.selectRegion(1);
        manager.startDrag(QPoint(160, 50));
        manager.updateDrag(QPoint(210, 50));
        manager.finishDrag();

        // The live canvas still begins at model x=0, so both the region and
        // annotation preview move right. Recomposed output crops the 50px
        // empty prefix and therefore applies no final annotation delta.
        QCOMPARE(manager.annotationTranslation(annotation, 1.0, false),
                 QPointF(50.0, 0.0));
        QCOMPARE(manager.annotationTranslation(annotation, 1.0, true),
                 QPointF(0.0, 0.0));
    }

    void testRedoOwnedAnnotationUsesFinalRegionCoordinates() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        AnnotationLayer layer;
        auto* annotation = addTrackingAnnotation(layer, QRect(130, 20, 10, 10));
        layer.undo();
        QCOMPARE(layer.itemCount(), static_cast<size_t>(0));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(1);
        manager.startDrag(QPoint(160, 50));
        manager.updateDrag(QPoint(200, 50));
        manager.finishDrag();
        manager.updateAnnotationPositions(&layer);

        layer.redo();
        QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
        QCOMPARE(annotation->boundingRect(), QRect(170, 20, 10, 10));
    }

    void testRemovedAnnotationUsesFinalRegionCoordinatesAfterUndo() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(2);
        AnnotationLayer layer;
        auto* annotation = addTrackingAnnotation(layer, QRect(130, 20, 10, 10));
        layer.setSelectedIndex(0);
        QVERIFY(layer.removeSelectedItem());
        QCOMPARE(layer.itemCount(), static_cast<size_t>(0));

        manager.enterLayoutMode(regions, QSize(210, 100));
        manager.selectRegion(1);
        manager.startDrag(QPoint(160, 50));
        manager.updateDrag(QPoint(200, 50));
        manager.finishDrag();
        manager.updateAnnotationPositions(&layer);

        layer.undo();
        QCOMPARE(layer.itemCount(), static_cast<size_t>(1));
        QCOMPARE(annotation->boundingRect(), QRect(170, 20, 10, 10));
    }

    void testRecomposeMaterializesCommittedRegionAtTargetDpr() {
        RegionLayoutManager manager;
        auto regions = createTestRegions(1);
        manager.enterLayoutMode(regions, QSize(100, 100));
        manager.selectRegion(0);
        manager.startResize(ResizeHandler::Edge::BottomRight, QPoint(99, 99));
        manager.updateResize(QPoint(149, 149), false);
        manager.finishResize();

        QVector<LayoutRegion> committedRegions;
        const QPixmap pixmap = manager.recomposeImage(2.0, &committedRegions);

        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.size(), QSize(300, 300));
        QCOMPARE(pixmap.devicePixelRatio(), 2.0);
        QCOMPARE(committedRegions.size(), 1);
        QCOMPARE(committedRegions[0].rect, QRect(0, 0, 150, 150));
        QCOMPARE(committedRegions[0].originalRect, committedRegions[0].rect);
        QCOMPARE(committedRegions[0].image.size(), QSize(300, 300));
        QCOMPARE(committedRegions[0].image.devicePixelRatio(), 2.0);
    }

    // =========================================================================
    // Serialization Tests
    // =========================================================================

    void testSerializeDeserializeRoundtrip() {
        auto regions = createTestRegions(3);

        QByteArray data = RegionLayoutManager::serializeRegions(regions);
        QVERIFY(!data.isEmpty());

        auto deserialized = RegionLayoutManager::deserializeRegions(data);
        QCOMPARE(deserialized.size(), regions.size());

        for (int i = 0; i < regions.size(); ++i) {
            QCOMPARE(deserialized[i].rect, regions[i].rect);
            QCOMPARE(deserialized[i].originalRect, regions[i].originalRect);
            QCOMPARE(deserialized[i].color, regions[i].color);
            QCOMPARE(deserialized[i].index, regions[i].index);
            QCOMPARE(deserialized[i].image.size(), regions[i].image.size());
        }
    }

    void testSerializeEmptyRegions() {
        QVector<LayoutRegion> empty;
        QByteArray data = RegionLayoutManager::serializeRegions(empty);
        QVERIFY(!data.isEmpty());  // Should still have header

        auto deserialized = RegionLayoutManager::deserializeRegions(data);
        QVERIFY(deserialized.isEmpty());
    }

    void testDeserializeCorruptData() {
        QByteArray corruptData = "invalid data";
        auto result = RegionLayoutManager::deserializeRegions(corruptData);
        QVERIFY(result.isEmpty());
    }

    void testDeserializeWrongMagic() {
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << quint32(0x12345678);  // Wrong magic
        stream << quint16(1);
        stream << quint32(0);

        auto result = RegionLayoutManager::deserializeRegions(data);
        QVERIFY(result.isEmpty());
    }
};

QTEST_MAIN(TestRegionLayoutManager)
#include "tst_RegionLayoutManager.moc"
