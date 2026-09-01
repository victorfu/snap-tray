#include <QtTest/QtTest>

#include "tools/ToolManager.h"
#include "tools/ToolWidthSlot.h"

class tst_ToolManagerWidthRouting : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultsAreIndependent();
    void testWidthIsPerSlotAndSurvivesToolSwitching();
    void testStrokeToolsContinueSharingOneSlot();
};

void tst_ToolManagerWidthRouting::testDefaultsAreIndependent()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), ToolWidthDefaults::kMosaicBrush);

    manager.setCurrentTool(ToolId::Pencil);
    QCOMPARE(manager.width(), ToolWidthDefaults::kStroke);
}

void tst_ToolManagerWidthRouting::testWidthIsPerSlotAndSurvivesToolSwitching()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Pencil);
    manager.setWidth(4);
    QCOMPARE(manager.width(), 4);

    manager.setCurrentTool(ToolId::Mosaic);
    manager.setWidth(ToolWidthDefaults::kMosaicBrushLarge);
    QCOMPARE(manager.width(), ToolWidthDefaults::kMosaicBrushLarge);

    manager.setCurrentTool(ToolId::Arrow);
    QCOMPARE(manager.width(), 4);

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), ToolWidthDefaults::kMosaicBrushLarge);
}

void tst_ToolManagerWidthRouting::testStrokeToolsContinueSharingOneSlot()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Pencil);
    manager.setWidth(5);

    manager.setCurrentTool(ToolId::Shape);
    QCOMPARE(manager.width(), 5);

    manager.setWidth(7);
    manager.setCurrentTool(ToolId::Polyline);
    QCOMPARE(manager.width(), 7);

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), ToolWidthDefaults::kMosaicBrush);
}

QTEST_MAIN(tst_ToolManagerWidthRouting)
#include "tst_ToolManagerWidthRouting.moc"
