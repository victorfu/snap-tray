#include <QtTest/QtTest>

#include "qml/PinToolOptionsViewModel.h"

#include <QSignalSpy>

class tst_PinToolOptionsViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testFontSizeDropdownRequested_PreservesCoordinates();
    void testFontFamilyDropdownRequested_PreservesCoordinates();
    void testArrowStyleDropdownRequested_PreservesCoordinates();
    void testLineStyleDropdownRequested_PreservesCoordinates();
    void testArrowStyleOptions_AreValueOnly();
    void testLineStyleOptions_AreValueOnly();
};

void tst_PinToolOptionsViewModel::testFontSizeDropdownRequested_PreservesCoordinates()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy spy(&viewModel, &PinToolOptionsViewModel::fontSizeDropdownRequested);

    viewModel.handleFontSizeDropdown(123.5, 456.25);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 123.5);
    QCOMPARE(spy.at(0).at(1).toDouble(), 456.25);
}

void tst_PinToolOptionsViewModel::testFontFamilyDropdownRequested_PreservesCoordinates()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy spy(&viewModel, &PinToolOptionsViewModel::fontFamilyDropdownRequested);

    viewModel.handleFontFamilyDropdown(222.0, 333.75);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 222.0);
    QCOMPARE(spy.at(0).at(1).toDouble(), 333.75);
}

void tst_PinToolOptionsViewModel::testArrowStyleDropdownRequested_PreservesCoordinates()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy spy(&viewModel, &PinToolOptionsViewModel::arrowStyleDropdownRequested);

    viewModel.handleArrowStyleDropdown(42.25, 84.5);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 42.25);
    QCOMPARE(spy.at(0).at(1).toDouble(), 84.5);
}

void tst_PinToolOptionsViewModel::testLineStyleDropdownRequested_PreservesCoordinates()
{
    PinToolOptionsViewModel viewModel;
    QSignalSpy spy(&viewModel, &PinToolOptionsViewModel::lineStyleDropdownRequested);

    viewModel.handleLineStyleDropdown(19.0, 73.125);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toDouble(), 19.0);
    QCOMPARE(spy.at(0).at(1).toDouble(), 73.125);
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

QTEST_MAIN(tst_PinToolOptionsViewModel)
#include "tst_PinToolOptionsViewModel.moc"
