#include <QtTest/QtTest>

#include "region/RegionToolbarHandler.h"
#include "tools/ToolManager.h"

class tst_ToolManagerWidthRouting : public QObject
{
    Q_OBJECT

private slots:
    void testWidthIsPerSlotAndSurvivesToolSwitching();
    void testMosaicDefaultIsIndependentOfStrokeDefault();
    void testTogglingMosaicKeepsToolManagerInSync();
};

void tst_ToolManagerWidthRouting::testWidthIsPerSlotAndSurvivesToolSwitching()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Pencil);
    manager.setWidth(4);
    QCOMPARE(manager.width(), 4);

    manager.setCurrentTool(ToolId::Mosaic);
    manager.setWidth(24);
    QCOMPARE(manager.width(), 24);

    manager.setCurrentTool(ToolId::Arrow);
    QCOMPARE(manager.width(), 4);

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), 24);
}

void tst_ToolManagerWidthRouting::testMosaicDefaultIsIndependentOfStrokeDefault()
{
    ToolManager manager;

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), 18);

    manager.setCurrentTool(ToolId::Pencil);
    QCOMPARE(manager.width(), 3);
}

void tst_ToolManagerWidthRouting::testTogglingMosaicKeepsToolManagerInSync()
{
    ToolManager manager;
    RegionToolbarHandler handler;
    handler.setToolManager(&manager);
    handler.setCurrentTool(ToolId::Mosaic);

    manager.setCurrentTool(ToolId::Mosaic);
    manager.setWidth(24);

    ToolId emittedTool = ToolId::Count;
    connect(&handler, &RegionToolbarHandler::toolChanged, this,
            [&emittedTool](ToolId tool, bool) { emittedTool = tool; });

    handler.handleToolbarClick(ToolId::Mosaic);

    QCOMPARE(emittedTool, ToolId::Selection);
    QCOMPARE(manager.currentTool(), ToolId::Selection);
    QCOMPARE(manager.width(), 3);

    manager.setCurrentTool(ToolId::Mosaic);
    QCOMPARE(manager.width(), 24);
}

QTEST_MAIN(tst_ToolManagerWidthRouting)
#include "tst_ToolManagerWidthRouting.moc"
