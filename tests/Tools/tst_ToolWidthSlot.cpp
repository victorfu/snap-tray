#include <QtTest/QtTest>

#include "tools/ToolWidthSlot.h"

class tst_ToolWidthSlot : public QObject
{
    Q_OBJECT

private slots:
    void testMosaicUsesItsOwnSlot();
    void testStrokeToolsShareOneSlot();
    void testUnlistedToolsDefaultToStroke();
};

void tst_ToolWidthSlot::testMosaicUsesItsOwnSlot()
{
    QCOMPARE(widthSlotForTool(ToolId::Mosaic), WidthSlot::MosaicBrush);
}

void tst_ToolWidthSlot::testStrokeToolsShareOneSlot()
{
    QCOMPARE(widthSlotForTool(ToolId::Pencil), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Arrow), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Shape), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Polyline), WidthSlot::Stroke);
}

void tst_ToolWidthSlot::testUnlistedToolsDefaultToStroke()
{
    QCOMPARE(widthSlotForTool(ToolId::Selection), WidthSlot::Stroke);
    QCOMPARE(widthSlotForTool(ToolId::Copy), WidthSlot::Stroke);
}

QTEST_APPLESS_MAIN(tst_ToolWidthSlot)
#include "tst_ToolWidthSlot.moc"
