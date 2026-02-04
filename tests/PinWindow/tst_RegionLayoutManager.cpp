#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QImage>
#include <QColor>

#include "pinwindow/RegionLayoutManager.h"

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
