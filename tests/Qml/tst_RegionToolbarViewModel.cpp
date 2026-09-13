#include <QtTest/QtTest>

#include "qml/RegionToolbarViewModel.h"

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

bool containsButtonWithIconKey(const QVariantList& buttons, const QString& iconKey)
{
    for (const QVariant& button : buttons) {
        const QVariantMap entry = button.toMap();
        if (entry.value(QStringLiteral("iconKey")).toString() == iconKey) {
            return true;
        }
    }

    return false;
}

} // namespace

class tst_RegionToolbarViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testNormalToolbarOmitsCancelRecordAndHandlesOcrAvailability();
    void testExportActionsRemainAvailable_data();
    void testExportActionsRemainAvailable();
};

void tst_RegionToolbarViewModel::testNormalToolbarOmitsCancelRecordAndHandlesOcrAvailability()
{
    RegionToolbarViewModel viewModel;
    const QVariantList buttons = viewModel.buttons();

    const QVariantMap cancelButton = findButtonById(buttons, static_cast<int>(ToolId::Cancel));
    const QVariantMap ocrButton = findButtonById(buttons, static_cast<int>(ToolId::OCR));

    QVERIFY(cancelButton.isEmpty());
    QVERIFY(!containsButtonWithIconKey(buttons, QStringLiteral("record")));
#if defined(Q_OS_LINUX)
    QVERIFY(ocrButton.isEmpty());
#else
    QVERIFY(!ocrButton.isEmpty());
    QVERIFY(ocrButton.value(QStringLiteral("separatorBefore")).toBool());
#endif
}

void tst_RegionToolbarViewModel::testExportActionsRemainAvailable_data()
{
    QTest::addColumn<int>("toolId");
    QTest::addColumn<QByteArray>("signal");
    QTest::newRow("pin") << static_cast<int>(ToolId::Pin) << QByteArray(SIGNAL(pinClicked()));
    QTest::newRow("save") << static_cast<int>(ToolId::Save) << QByteArray(SIGNAL(saveClicked()));
    QTest::newRow("copy") << static_cast<int>(ToolId::Copy) << QByteArray(SIGNAL(copyClicked()));
}

void tst_RegionToolbarViewModel::testExportActionsRemainAvailable()
{
    QFETCH(int, toolId);
    QFETCH(QByteArray, signal);
    RegionToolbarViewModel viewModel;
    const QVariantList buttons = viewModel.buttons();
    for (const QVariant& button : buttons) {
        QVERIFY(button.toMap().value(QStringLiteral("iconKey")).toString() != QStringLiteral("share"));
    }
    const QVariantMap exportButton = findButtonById(buttons, toolId);
    QVERIFY(!exportButton.isEmpty());
    QVERIFY(exportButton.value(QStringLiteral("isAction")).toBool());
    QVERIFY(exportButton.value(QStringLiteral("isExportAction")).toBool());

    QSignalSpy actionSpy(&viewModel, signal.constData());
    QSignalSpy drawingSpy(&viewModel, &RegionToolbarViewModel::toolSelected);
    QVERIFY(actionSpy.isValid());
    viewModel.handleButtonClicked(toolId);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(drawingSpy.count(), 0);
}

QTEST_MAIN(tst_RegionToolbarViewModel)
#include "tst_RegionToolbarViewModel.moc"
