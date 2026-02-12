#include <QtTest>

#include "region/MultiRegionManager.h"

class tst_MultiRegionManagerReorder : public QObject
{
    Q_OBJECT

private slots:
    void testMoveRegionPreservesIdentity();
    void testMoveRegionNoOp();
};

void tst_MultiRegionManagerReorder::testMoveRegionPreservesIdentity()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 120, 90));
    manager.addRegion(QRect(150, 15, 80, 60));
    manager.addRegion(QRect(260, 30, 70, 50));

    const QVector<MultiRegionManager::Region> before = manager.regions();
    QCOMPARE(before.size(), 3);
    QCOMPARE(manager.activeIndex(), 2);

    QSignalSpy reorderedSpy(&manager, &MultiRegionManager::regionsReordered);
    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);

    QVERIFY(manager.moveRegion(0, 2));
    QCOMPARE(reorderedSpy.count(), 1);
    QCOMPARE(activeSpy.count(), 1);

    const QVector<MultiRegionManager::Region> after = manager.regions();
    QCOMPARE(after.size(), 3);

    // Reorder keeps region identity (index/color/rect) and only changes sequence.
    QCOMPARE(after[0].rect, before[1].rect);
    QCOMPARE(after[0].index, before[1].index);
    QCOMPARE(after[0].color, before[1].color);

    QCOMPARE(after[1].rect, before[2].rect);
    QCOMPARE(after[1].index, before[2].index);
    QCOMPARE(after[1].color, before[2].color);

    QCOMPARE(after[2].rect, before[0].rect);
    QCOMPARE(after[2].index, before[0].index);
    QCOMPARE(after[2].color, before[0].color);

    // Active region should still reference the previously active region.
    QCOMPARE(manager.activeIndex(), 1);
    QCOMPARE(after[manager.activeIndex()].rect, before[2].rect);
}

void tst_MultiRegionManagerReorder::testMoveRegionNoOp()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(0, 0, 100, 80));
    manager.addRegion(QRect(120, 0, 90, 60));

    QSignalSpy reorderedSpy(&manager, &MultiRegionManager::regionsReordered);

    QVERIFY(!manager.moveRegion(0, 0));
    QVERIFY(!manager.moveRegion(-1, 0));
    QVERIFY(!manager.moveRegion(0, 5));
    QCOMPARE(reorderedSpy.count(), 0);
}

QTEST_MAIN(tst_MultiRegionManagerReorder)
#include "tst_MultiRegionManagerReorder.moc"
