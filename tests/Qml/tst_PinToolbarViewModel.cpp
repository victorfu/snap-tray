#include <QtTest/QtTest>

#include "qml/PinToolbarViewModel.h"

#include <QSignalSpy>

namespace {

QVariantMap findButtonById(const QVariantList& buttons, int buttonId)
{
    for (const QVariant& button : buttons) {
        const QVariantMap entry = button.toMap();
        if (entry.value(QStringLiteral("id")).toInt() == buttonId) {
            return entry;
        }
    }

    return {};
}

int indexOfButtonId(const QVariantList& buttons, int buttonId)
{
    for (int index = 0; index < buttons.size(); ++index) {
        if (buttons.at(index).toMap().value(QStringLiteral("id")).toInt() == buttonId) {
            return index;
        }
    }

    return -1;
}

} // namespace

class tst_PinToolbarViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testBeautifyStartsProcessingSectionBeforeCropAndMeasure();
    void testBeautifyUsesDefaultIconStyling();
    void testBeautifyButtonEmitsBeautifySignal();
};

void tst_PinToolbarViewModel::testBeautifyStartsProcessingSectionBeforeCropAndMeasure()
{
    PinToolbarViewModel viewModel;
    const QVariantList buttons = viewModel.buttons();

    const int beautifyId = static_cast<int>(ToolId::Beautify);
    const int cropId = static_cast<int>(ToolId::Crop);
    const int measureId = static_cast<int>(ToolId::Measure);

    const QVariantMap beautifyButton = findButtonById(buttons, beautifyId);
    const QVariantMap cropButton = findButtonById(buttons, cropId);
    const QVariantMap measureButton = findButtonById(buttons, measureId);

    QVERIFY(!beautifyButton.isEmpty());
    QVERIFY(!cropButton.isEmpty());
    QVERIFY(!measureButton.isEmpty());

    QVERIFY(beautifyButton.value(QStringLiteral("separatorBefore")).toBool());
    QVERIFY(!cropButton.value(QStringLiteral("separatorBefore")).toBool());
    QVERIFY(!measureButton.value(QStringLiteral("separatorBefore")).toBool());

    const int beautifyIndex = indexOfButtonId(buttons, beautifyId);
    const int cropIndex = indexOfButtonId(buttons, cropId);
    const int measureIndex = indexOfButtonId(buttons, measureId);

    QVERIFY(beautifyIndex >= 0);
    QVERIFY(cropIndex >= 0);
    QVERIFY(measureIndex >= 0);
    QVERIFY(beautifyIndex < cropIndex);
    QVERIFY(cropIndex < measureIndex);
}

void tst_PinToolbarViewModel::testBeautifyUsesDefaultIconStyling()
{
    PinToolbarViewModel viewModel;
    const QVariantMap beautifyButton = findButtonById(
        viewModel.buttons(), static_cast<int>(ToolId::Beautify));

    QVERIFY(!beautifyButton.isEmpty());
    QVERIFY(!beautifyButton.value(QStringLiteral("isAction")).toBool());
    QCOMPARE(beautifyButton.value(QStringLiteral("iconSource")).toString(),
             QStringLiteral("qrc:/icons/icons/beautify.svg"));
}

void tst_PinToolbarViewModel::testBeautifyButtonEmitsBeautifySignal()
{
    PinToolbarViewModel viewModel;
    QSignalSpy toolSpy(&viewModel, &PinToolbarViewModel::toolSelected);
    QSignalSpy beautifySpy(&viewModel, &PinToolbarViewModel::beautifyClicked);

    viewModel.handleButtonClicked(static_cast<int>(ToolId::Beautify));

    QCOMPARE(toolSpy.count(), 0);
    QCOMPARE(beautifySpy.count(), 1);
}

QTEST_MAIN(tst_PinToolbarViewModel)
#include "tst_PinToolbarViewModel.moc"
