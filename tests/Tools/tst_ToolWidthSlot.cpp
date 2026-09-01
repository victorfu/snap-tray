#include <QtTest/QtTest>

#include "tools/ToolWidthSlot.h"

class tst_ToolWidthSlot : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultsAndPresets();
    void testMosaicPresetValidation_data();
    void testMosaicPresetValidation();
    void testMosaicUsesItsOwnSlot();
    void testStrokeToolsShareOneSlot();
    void testUnlistedToolsDefaultToStroke();
};

void tst_ToolWidthSlot::testDefaultsAndPresets()
{
    QCOMPARE(ToolWidthDefaults::kStroke, 3);
    QCOMPARE(ToolWidthDefaults::kMosaicBrushSmall, 10);
    QCOMPARE(ToolWidthDefaults::kMosaicBrush, 18);
    QCOMPARE(ToolWidthDefaults::kMosaicBrushLarge, 30);
}

void tst_ToolWidthSlot::testMosaicPresetValidation_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<bool>("expected");

    QTest::newRow("small") << 10 << true;
    QTest::newRow("medium") << 18 << true;
    QTest::newRow("large") << 30 << true;
    QTest::newRow("below-small") << 9 << false;
    QTest::newRow("between-small-medium") << 17 << false;
    QTest::newRow("between-medium-large") << 29 << false;
    QTest::newRow("above-large") << 31 << false;
}

void tst_ToolWidthSlot::testMosaicPresetValidation()
{
    QFETCH(int, width);
    QFETCH(bool, expected);

    QCOMPARE(isMosaicWidthPreset(width), expected);
}

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
