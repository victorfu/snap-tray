#include <QtTest>

#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include "region/MultiRegionListPanel.h"

class tst_MultiRegionListPanel : public QObject
{
    Q_OBJECT

private slots:
    void testSignalsAndRenderingData();
    void testRowsMovedSignalMapping();
    void testCursorOverridesParentCrosshair();
    void testInteractionCursorOverrideAndRestore();
};

void tst_MultiRegionListPanel::testSignalsAndRenderingData()
{
    MultiRegionListPanel panel;
    panel.updatePanelGeometry(QSize(1200, 800));

    QPixmap background(500, 300);
    background.fill(QColor(30, 60, 90));
    panel.setCaptureContext(background, 1.0);

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

    panel.setRegions(regions);

    auto* list = panel.findChild<QListWidget*>("multiRegionListWidget");
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);

    QSignalSpy activateSpy(&panel, &MultiRegionListPanel::regionActivated);
    QSignalSpy deleteSpy(&panel, &MultiRegionListPanel::regionDeleteRequested);
    QSignalSpy replaceSpy(&panel, &MultiRegionListPanel::regionReplaceRequested);

    list->setCurrentRow(1);
    QCOMPARE(activateSpy.count(), 1);
    QCOMPARE(activateSpy.takeFirst().at(0).toInt(), 1);

    QWidget* itemWidget = list->itemWidget(list->item(0));
    QVERIFY(itemWidget != nullptr);

    auto* replaceButton = itemWidget->findChild<QPushButton*>("replaceButton");
    auto* deleteButton = itemWidget->findChild<QPushButton*>("deleteButton");
    QVERIFY(replaceButton != nullptr);
    QVERIFY(deleteButton != nullptr);

    replaceButton->click();
    QCOMPARE(replaceSpy.count(), 1);
    QCOMPARE(replaceSpy.takeFirst().at(0).toInt(), 0);

    deleteButton->click();
    QCOMPARE(deleteSpy.count(), 1);
    QCOMPARE(deleteSpy.takeFirst().at(0).toInt(), 0);
}

void tst_MultiRegionListPanel::testRowsMovedSignalMapping()
{
    MultiRegionListPanel panel;
    panel.updatePanelGeometry(QSize(1200, 800));

    QVector<MultiRegionManager::Region> regions;
    for (int i = 0; i < 3; ++i) {
        MultiRegionManager::Region region;
        region.rect = QRect(10 + i * 100, 10, 80, 60);
        region.index = i + 1;
        region.color = QColor(20 + i * 30, 100, 150);
        regions.push_back(region);
    }
    panel.setRegions(regions);

    auto* list = panel.findChild<QListWidget*>("multiRegionListWidget");
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 3);

    QSignalSpy moveSpy(&panel, &MultiRegionListPanel::regionMoveRequested);
    QVERIFY(list->model()->moveRow(QModelIndex(), 0, QModelIndex(), 3));
    QCOMPARE(moveSpy.count(), 1);
    const QList<QVariant> args = moveSpy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toInt(), 2);
}

void tst_MultiRegionListPanel::testCursorOverridesParentCrosshair()
{
    QWidget parent;
    parent.setCursor(Qt::CrossCursor);

    MultiRegionListPanel panel(&parent);
    panel.updatePanelGeometry(QSize(1200, 800));

    MultiRegionManager::Region region;
    region.rect = QRect(10, 10, 120, 80);
    region.index = 1;
    region.color = QColor(0, 174, 255);
    panel.setRegions({region});

    auto* list = panel.findChild<QListWidget*>("multiRegionListWidget");
    QVERIFY(list != nullptr);
    QVERIFY(list->count() == 1);
    QVERIFY(list->viewport() != nullptr);

    QCOMPARE(panel.cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(list->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(list->viewport()->cursor().shape(), Qt::ArrowCursor);

    QWidget* itemWidget = list->itemWidget(list->item(0));
    QVERIFY(itemWidget != nullptr);
    QCOMPARE(itemWidget->cursor().shape(), Qt::ArrowCursor);

    const auto labels = itemWidget->findChildren<QLabel*>();
    QVERIFY(!labels.isEmpty());
    for (const auto* label : labels) {
        QCOMPARE(label->cursor().shape(), Qt::ArrowCursor);
    }

    auto* replaceButton = itemWidget->findChild<QPushButton*>("replaceButton");
    auto* deleteButton = itemWidget->findChild<QPushButton*>("deleteButton");
    QVERIFY(replaceButton != nullptr);
    QVERIFY(deleteButton != nullptr);
    QCOMPARE(replaceButton->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(deleteButton->cursor().shape(), Qt::ArrowCursor);
}

void tst_MultiRegionListPanel::testInteractionCursorOverrideAndRestore()
{
    QWidget parent;
    parent.setCursor(Qt::CrossCursor);

    MultiRegionListPanel panel(&parent);
    panel.updatePanelGeometry(QSize(1200, 800));

    MultiRegionManager::Region region;
    region.rect = QRect(10, 10, 120, 80);
    region.index = 1;
    region.color = QColor(0, 174, 255);
    panel.setRegions({region});

    auto* list = panel.findChild<QListWidget*>("multiRegionListWidget");
    QVERIFY(list != nullptr);
    QVERIFY(list->count() == 1);
    QVERIFY(list->viewport() != nullptr);

    QWidget* itemWidget = list->itemWidget(list->item(0));
    QVERIFY(itemWidget != nullptr);
    auto* replaceButton = itemWidget->findChild<QPushButton*>("replaceButton");
    auto* deleteButton = itemWidget->findChild<QPushButton*>("deleteButton");
    QVERIFY(replaceButton != nullptr);
    QVERIFY(deleteButton != nullptr);

    panel.setInteractionCursor(Qt::SizeAllCursor);
    QCOMPARE(panel.cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(list->cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(list->viewport()->cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(itemWidget->cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(replaceButton->cursor().shape(), Qt::SizeAllCursor);
    QCOMPARE(deleteButton->cursor().shape(), Qt::SizeAllCursor);

    panel.clearInteractionCursor();
    QCOMPARE(panel.cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(list->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(list->viewport()->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(itemWidget->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(replaceButton->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(deleteButton->cursor().shape(), Qt::ArrowCursor);
}

QTEST_MAIN(tst_MultiRegionListPanel)
#include "tst_MultiRegionListPanel.moc"
