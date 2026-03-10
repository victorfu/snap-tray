#include <QtTest/QtTest>
#include <QVariantList>
#include <QVariantMap>
#include <QSignalSpy>

#include "qml/MultiRegionListViewModel.h"

class tst_MultiRegionListPanel : public QObject
{
    Q_OBJECT

private slots:
    void testRegionCount();
    void testRegionStructure();
    void testActiveIndex();
    void testSignals();
    void testMoveSignal();
};

void tst_MultiRegionListPanel::testRegionCount()
{
    MultiRegionListViewModel vm;

    QPixmap background(500, 300);
    background.fill(QColor(30, 60, 90));
    vm.setCaptureContext(background, 1.0);

    QVector<MultiRegionManager::Region> regions;
    MultiRegionManager::Region first;
    first.rect = QRect(10, 10, 120, 80);
    first.index = 1;
    first.color = QColor(0, 174, 255);
    regions.push_back(first);

    MultiRegionManager::Region second;
    second.rect = QRect(170, 20, 150, 90);
    second.index = 2;
    second.color = QColor(52, 199, 89);
    regions.push_back(second);

    vm.setRegions(regions);
    QCOMPARE(vm.count(), 2);
    QCOMPARE(vm.regions().size(), 2);
}

void tst_MultiRegionListPanel::testRegionStructure()
{
    MultiRegionListViewModel vm;

    QPixmap background(500, 300);
    background.fill(Qt::black);
    vm.setCaptureContext(background, 1.0);

    QVector<MultiRegionManager::Region> regions;
    MultiRegionManager::Region region;
    region.rect = QRect(10, 10, 120, 80);
    region.index = 1;
    region.color = QColor(0, 174, 255);
    regions.push_back(region);

    vm.setRegions(regions);

    QVariantMap map = vm.regions().first().toMap();
    QVERIFY(map.contains("index"));
    QVERIFY(map.contains("width"));
    QVERIFY(map.contains("height"));
    QVERIFY(map.contains("colorHex"));
    QVERIFY(map.contains("thumbnailId"));
    QCOMPARE(map["index"].toInt(), 1);
    QCOMPARE(map["width"].toInt(), 120);
    QCOMPARE(map["height"].toInt(), 80);
}

void tst_MultiRegionListPanel::testActiveIndex()
{
    MultiRegionListViewModel vm;
    QCOMPARE(vm.activeIndex(), -1);

    vm.setActiveIndex(2);
    QCOMPARE(vm.activeIndex(), 2);
}

void tst_MultiRegionListPanel::testSignals()
{
    MultiRegionListViewModel vm;

    QSignalSpy activateSpy(&vm, &MultiRegionListViewModel::regionActivated);
    QSignalSpy deleteSpy(&vm, &MultiRegionListViewModel::regionDeleteRequested);
    QSignalSpy replaceSpy(&vm, &MultiRegionListViewModel::regionReplaceRequested);

    vm.activateRegion(1);
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toInt(), 1);

    vm.deleteRegion(0);
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(deleteSpy.takeFirst().at(0).toInt(), 0);

    vm.replaceRegion(2);
    QCOMPARE(replaceSpy.count(), 1);
    QCOMPARE(replaceSpy.takeFirst().at(0).toInt(), 2);
}

void tst_MultiRegionListPanel::testMoveSignal()
{
    MultiRegionListViewModel vm;

    QSignalSpy moveSpy(&vm, &MultiRegionListViewModel::regionMoveRequested);

    vm.moveRegion(0, 2);
    QCOMPARE(moveSpy.count(), 1);
    const QList<QVariant> args = moveSpy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toInt(), 2);
}

QTEST_MAIN(tst_MultiRegionListPanel)
#include "tst_MultiRegionListPanel.moc"
