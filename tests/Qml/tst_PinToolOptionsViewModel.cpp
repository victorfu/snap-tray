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
    void testMosaicWidthPresetOptions_HaveExpectedValues();
    void testMosaicWidthPresetSelection_UpdatesWidth();
    void testMosaicWidthPresetSelection_IgnoresInactiveAndInvalidValues();
    void testMosaicWidthWheel_IsNotHandled();
    void testNonMosaicWidthWheel_RemainsHandled();
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

void tst_PinToolOptionsViewModel::testMosaicWidthPresetOptions_HaveExpectedValues()
{
    PinToolOptionsViewModel viewModel;
    const QVariantList options = viewModel.mosaicWidthPresetOptions();

    QCOMPARE(options.size(), 3);

    const int expectedWidths[] = {10, 18, 30};
    const int expectedPreviewDiameters[] = {6, 10, 14};
    for (int i = 0; i < options.size(); ++i) {
        const QVariantMap option = options.at(i).toMap();
        QCOMPARE(option.size(), 2);
        QCOMPARE(option.value(QStringLiteral("value")).toInt(), expectedWidths[i]);
        QCOMPARE(option.value(QStringLiteral("previewDiameter")).toInt(),
                 expectedPreviewDiameters[i]);
    }
}

void tst_PinToolOptionsViewModel::testMosaicWidthPresetSelection_UpdatesWidth()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy widthSpy(&viewModel, &PinToolOptionsViewModel::widthValueChanged);

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    const int expectedWidths[] = {10, 18, 30};
    for (int i = 0; i < 3; ++i) {
        const int width = expectedWidths[i];
        viewModel.handleMosaicWidthPresetSelected(width);
        QCOMPARE(viewModel.currentWidth(), width);
        QCOMPARE(widthSpy.count(), i + 1);
        QCOMPARE(widthSpy.at(i).at(0).toInt(), width);
    }
}

void tst_PinToolOptionsViewModel::testMosaicWidthPresetSelection_IgnoresInactiveAndInvalidValues()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy widthSpy(&viewModel, &PinToolOptionsViewModel::widthValueChanged);

    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    viewModel.handleMosaicWidthPresetSelected(10);

    QCOMPARE(viewModel.currentWidth(), 3);
    QCOMPARE(widthSpy.count(), 0);

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    viewModel.handleMosaicWidthPresetSelected(9);
    viewModel.handleMosaicWidthPresetSelected(31);

    QCOMPARE(viewModel.currentWidth(), 3);
    QCOMPARE(widthSpy.count(), 0);
}

void tst_PinToolOptionsViewModel::testMosaicWidthWheel_IsNotHandled()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy widthSpy(&viewModel, &PinToolOptionsViewModel::widthValueChanged);
    QSignalSpy currentWidthSpy(&viewModel, &PinToolOptionsViewModel::currentWidthChanged);

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Count));
    QVERIFY(!viewModel.isMosaicActive());

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Mosaic));
    QVERIFY(viewModel.isMosaicActive());
    QVERIFY(!viewModel.handleWidthWheelDelta(120));
    QVERIFY(!viewModel.handleWidthWheelDelta(-120));
    QVERIFY(!viewModel.handleWidthWheelDelta(0));
    QCOMPARE(viewModel.currentWidth(), 3);
    QCOMPARE(widthSpy.count(), 0);
    QCOMPARE(currentWidthSpy.count(), 0);
}

void tst_PinToolOptionsViewModel::testNonMosaicWidthWheel_RemainsHandled()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy widthSpy(&viewModel, &PinToolOptionsViewModel::widthValueChanged);

    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    QVERIFY(viewModel.handleWidthWheelDelta(120));
    QCOMPARE(viewModel.currentWidth(), 4);
    QCOMPARE(widthSpy.count(), 1);
    QCOMPARE(widthSpy.at(0).at(0).toInt(), 4);

    QVERIFY(viewModel.handleWidthWheelDelta(-120));
    QCOMPARE(viewModel.currentWidth(), 3);
    QCOMPARE(widthSpy.count(), 2);
    QCOMPARE(widthSpy.at(1).at(0).toInt(), 3);
}

void tst_PinToolOptionsViewModel::testLaserPointerOptions_ClearMosaicSemantics()
{
    PinToolOptionsViewModel viewModel;

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    QVERIFY(viewModel.isMosaicActive());

    viewModel.showLaserPointerOptions();

    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Selection));
    QVERIFY(!viewModel.isMosaicActive());
    QVERIFY(viewModel.handleWidthWheelDelta(120));

    viewModel.showForTool(static_cast<int>(ToolId::Mosaic));
    QVERIFY(!viewModel.handleWidthWheelDelta(120));
    viewModel.clearSections();
    QCOMPARE(viewModel.activeToolId(), static_cast<int>(ToolId::Count));
    QVERIFY(!viewModel.isMosaicActive());
    QVERIFY(!viewModel.handleWidthWheelDelta(120));
}

QTEST_MAIN(tst_PinToolOptionsViewModel)
#include "tst_PinToolOptionsViewModel.moc"
