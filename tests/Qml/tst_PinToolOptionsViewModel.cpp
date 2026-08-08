#include <QtTest/QtTest>

#include "qml/PinToolOptionsViewModel.h"

#include <QSignalSpy>

namespace {
enum class DropdownHandler {
    FontSize,
    FontFamily,
    ArrowStyle,
    LineStyle
};
}

Q_DECLARE_METATYPE(DropdownHandler)

class tst_PinToolOptionsViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testDropdownRequested_PreservesCoordinates_data();
    void testDropdownRequested_PreservesCoordinates();
    void testArrowStyleOptions_AreValueOnly();
    void testLineStyleOptions_AreValueOnly();
    void testShowLaserPointerOptions_ShowsOnlyColorAndWidth();
    void testClearSections_DisablesWidthWheelHandling();
    void testAutoBlurHintText();
    void testMosaicWheelGesture_EmitsLearnedSignal();
    void testProgrammaticAndNonMosaicWidthChanges_DoNotLearn();
    void testLaserPointerOptions_ClearMosaicSemantics();
};

void tst_PinToolOptionsViewModel::testDropdownRequested_PreservesCoordinates_data()
{
    QTest::addColumn<DropdownHandler>("handler");
    QTest::addColumn<QByteArray>("signal");
    QTest::addColumn<double>("x");
    QTest::addColumn<double>("y");

    QTest::newRow("font-size") << DropdownHandler::FontSize
                                << QByteArray(SIGNAL(fontSizeDropdownRequested(double, double)))
                                << 123.5 << 456.25;
    QTest::newRow("font-family") << DropdownHandler::FontFamily
                                  << QByteArray(SIGNAL(fontFamilyDropdownRequested(double, double)))
                                  << 222.0 << 333.75;
    QTest::newRow("arrow-style") << DropdownHandler::ArrowStyle
                                  << QByteArray(SIGNAL(arrowStyleDropdownRequested(double, double)))
                                  << 42.25 << 84.5;
    QTest::newRow("line-style") << DropdownHandler::LineStyle
                                 << QByteArray(SIGNAL(lineStyleDropdownRequested(double, double)))
                                 << 19.0 << 73.125;
}

void tst_PinToolOptionsViewModel::testDropdownRequested_PreservesCoordinates()
{
    QFETCH(DropdownHandler, handler);
    QFETCH(QByteArray, signal);
    QFETCH(double, x);
    QFETCH(double, y);

    PinToolOptionsViewModel viewModel;
    QSignalSpy spy(&viewModel, signal.constData());
    QVERIFY(spy.isValid());

    switch (handler) {
    case DropdownHandler::FontSize:
        viewModel.handleFontSizeDropdown(x, y);
        break;
    case DropdownHandler::FontFamily:
        viewModel.handleFontFamilyDropdown(x, y);
        break;
    case DropdownHandler::ArrowStyle:
        viewModel.handleArrowStyleDropdown(x, y);
        break;
    case DropdownHandler::LineStyle:
        viewModel.handleLineStyleDropdown(x, y);
        break;
    }

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), x);
    QCOMPARE(spy.at(0).at(1).toDouble(), y);
}

void tst_PinToolOptionsViewModel::testArrowStyleOptions_AreValueOnly()
{
    PinToolOptionsViewModel viewModel;
    const QVariantList options = viewModel.arrowStyleOptions();

    QCOMPARE(options.size(), 6);
    for (int i = 0; i < options.size(); ++i) {
        const QVariantMap option = options.at(i).toMap();
        QCOMPARE(option.value(QStringLiteral("value")).toInt(), i);
        QVERIFY(!option.contains(QStringLiteral("iconKey")));
    }
}

void tst_PinToolOptionsViewModel::testLineStyleOptions_AreValueOnly()
{
    PinToolOptionsViewModel viewModel;
    const QVariantList options = viewModel.lineStyleOptions();

    QCOMPARE(options.size(), 3);
    for (int i = 0; i < options.size(); ++i) {
        const QVariantMap option = options.at(i).toMap();
        QCOMPARE(option.value(QStringLiteral("value")).toInt(), i);
        QVERIFY(!option.contains(QStringLiteral("iconKey")));
    }
}

void tst_PinToolOptionsViewModel::testShowLaserPointerOptions_ShowsOnlyColorAndWidth()
{
    PinToolOptionsViewModel viewModel;

    viewModel.showLaserPointerOptions();

    QVERIFY(viewModel.showColorSection());
    QVERIFY(viewModel.showWidthSection());
    QVERIFY(!viewModel.showLineStyleSection());
    QVERIFY(!viewModel.showArrowStyleSection());
    QVERIFY(!viewModel.showTextSection());
}

void tst_PinToolOptionsViewModel::testClearSections_DisablesWidthWheelHandling()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showLaserPointerOptions();

    QVERIFY(viewModel.handleWidthWheelDelta(120));

    viewModel.clearSections();

    QVERIFY(!viewModel.handleWidthWheelDelta(120));
}

void tst_PinToolOptionsViewModel::testAutoBlurHintText()
{
    PinToolOptionsViewModel viewModel;
    QCOMPARE(viewModel.autoBlurHintText(),
             QStringLiteral("Automatically detect and blur faces and credentials"));
}

void tst_PinToolOptionsViewModel::testMosaicWheelGesture_EmitsLearnedSignal()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy learnedSpy(
        &viewModel, &PinToolOptionsViewModel::mosaicBrushAdjustmentLearned);

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Count));
    QVERIFY(!viewModel.isMosaicActive());
    QCOMPARE(viewModel.mosaicBrushHintText(),
             QStringLiteral("Scroll here to adjust the mosaic brush size"));

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Mosaic));
    QVERIFY(viewModel.isMosaicActive());
    QVERIFY(viewModel.handleWidthWheelDelta(120));
    QCOMPARE(learnedSpy.count(), 1);

    QVERIFY(viewModel.handleWidthWheelDelta(-120));
    QCOMPARE(learnedSpy.count(), 2);
    QVERIFY(!viewModel.handleWidthWheelDelta(0));
    QCOMPARE(learnedSpy.count(), 2);
}

void tst_PinToolOptionsViewModel::testProgrammaticAndNonMosaicWidthChanges_DoNotLearn()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy learnedSpy(
        &viewModel, &PinToolOptionsViewModel::mosaicBrushAdjustmentLearned);

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.setCurrentWidth(12);
    viewModel.handleWidthChanged(13);
    QCOMPARE(learnedSpy.count(), 0);

    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    QVERIFY(viewModel.handleWidthWheelDelta(120));
    QCOMPARE(learnedSpy.count(), 0);
}

void tst_PinToolOptionsViewModel::testLaserPointerOptions_ClearMosaicSemantics()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy learnedSpy(
        &viewModel, &PinToolOptionsViewModel::mosaicBrushAdjustmentLearned);

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    QVERIFY(viewModel.isMosaicActive());

    viewModel.showLaserPointerOptions();

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Selection));
    QVERIFY(!viewModel.isMosaicActive());
    QVERIFY(viewModel.handleWidthWheelDelta(120));
    QCOMPARE(learnedSpy.count(), 0);

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.clearSections();
    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Count));
    QVERIFY(!viewModel.isMosaicActive());
    QVERIFY(!viewModel.handleWidthWheelDelta(120));
    QCOMPARE(learnedSpy.count(), 0);
}

QTEST_MAIN(tst_PinToolOptionsViewModel)
#include "tst_PinToolOptionsViewModel.moc"
