#include <QtTest>

#include "region/MultiRegionManager.h"

class tst_MultiRegionManagerDeleteReindex : public QObject
{
    Q_OBJECT

private slots:
    void testDeleteCompactsIndicesAndColors();
    void testDeleteActiveRegionEmitsAndReassignsWhenIndexMatches();
    void testDeleteLastActiveRegionEmitsAndMovesToPrevious();
    void testDeleteNonActiveRegionAfterActiveKeepsSelectionWithoutSignal();
    void testClearFromNonEmptyResetsActiveAndEmits();
    void testClearWithoutActiveDoesNotEmitActiveSignal();
};

void tst_MultiRegionManagerDeleteReindex::testDeleteCompactsIndicesAndColors()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));
    manager.addRegion(QRect(260, 10, 120, 70));

    const QVector<MultiRegionManager::Region> before = manager.regions();
    QCOMPARE(before.size(), 3);

    QSignalSpy removedSpy(&manager, &MultiRegionManager::regionRemoved);
    manager.removeRegion(0);

    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(manager.count(), 2);
    QCOMPARE(manager.activeIndex(), 1);

    const QVector<MultiRegionManager::Region> after = manager.regions();
    QCOMPARE(after.size(), 2);

    // Remaining rect order should compact.
    QCOMPARE(after[0].rect, before[1].rect);
    QCOMPARE(after[1].rect, before[2].rect);

    // Delete path reindexes and recolors.
    QCOMPARE(after[0].index, 1);
    QCOMPARE(after[1].index, 2);
    QCOMPARE(after[0].color, QColor(0, 174, 255));
    QCOMPARE(after[1].color, QColor(52, 199, 89));
}

void tst_MultiRegionManagerDeleteReindex::testDeleteActiveRegionEmitsAndReassignsWhenIndexMatches()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));
    manager.addRegion(QRect(260, 10, 120, 70));
    manager.setActiveIndex(1);

    const QRect firstRect = manager.regionRect(0);
    const QRect thirdRect = manager.regionRect(2);

    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);
    QSignalSpy removedSpy(&manager, &MultiRegionManager::regionRemoved);
    manager.removeRegion(1);

    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(activeSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(manager.count(), 2);
    QCOMPARE(manager.activeIndex(), 1);

    const QVector<MultiRegionManager::Region> after = manager.regions();
    QCOMPARE(after.size(), 2);
    QCOMPARE(after[0].rect, firstRect);
    QCOMPARE(after[1].rect, thirdRect);
    QVERIFY(!after[0].isActive);
    QVERIFY(after[1].isActive);
}

void tst_MultiRegionManagerDeleteReindex::testDeleteLastActiveRegionEmitsAndMovesToPrevious()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));
    manager.addRegion(QRect(260, 10, 120, 70));

    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);
    manager.removeRegion(2);

    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(activeSpy.at(0).at(0).toInt(), 1);
    QCOMPARE(manager.count(), 2);
    QCOMPARE(manager.activeIndex(), 1);

    const QVector<MultiRegionManager::Region> after = manager.regions();
    QCOMPARE(after.size(), 2);
    QVERIFY(!after[0].isActive);
    QVERIFY(after[1].isActive);
}

void tst_MultiRegionManagerDeleteReindex::testDeleteNonActiveRegionAfterActiveKeepsSelectionWithoutSignal()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));
    manager.addRegion(QRect(260, 10, 120, 70));
    manager.setActiveIndex(0);

    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);
    manager.removeRegion(2);

    QCOMPARE(activeSpy.count(), 0);
    QCOMPARE(manager.count(), 2);
    QCOMPARE(manager.activeIndex(), 0);

    const QVector<MultiRegionManager::Region> after = manager.regions();
    QCOMPARE(after.size(), 2);
    QVERIFY(after[0].isActive);
    QVERIFY(!after[1].isActive);
}

void tst_MultiRegionManagerDeleteReindex::testClearFromNonEmptyResetsActiveAndEmits()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));

    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);
    QSignalSpy clearedSpy(&manager, &MultiRegionManager::regionsCleared);

    manager.clear();

    QCOMPARE(clearedSpy.count(), 1);
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(activeSpy.at(0).at(0).toInt(), -1);
    QCOMPARE(manager.count(), 0);
    QCOMPARE(manager.activeIndex(), -1);
}

void tst_MultiRegionManagerDeleteReindex::testClearWithoutActiveDoesNotEmitActiveSignal()
{
    MultiRegionManager manager;
    manager.addRegion(QRect(10, 10, 100, 70));
    manager.addRegion(QRect(130, 10, 110, 70));
    manager.setActiveIndex(-1);

    QSignalSpy activeSpy(&manager, &MultiRegionManager::activeIndexChanged);
    QSignalSpy clearedSpy(&manager, &MultiRegionManager::regionsCleared);

    manager.clear();

    QCOMPARE(clearedSpy.count(), 1);
    QCOMPARE(activeSpy.count(), 0);
    QCOMPARE(manager.count(), 0);
    QCOMPARE(manager.activeIndex(), -1);
}

QTEST_MAIN(tst_MultiRegionManagerDeleteReindex)
#include "tst_MultiRegionManagerDeleteReindex.moc"
