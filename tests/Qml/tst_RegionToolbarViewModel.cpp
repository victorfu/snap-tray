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

QTEST_MAIN(tst_RegionToolbarViewModel)
#include "tst_RegionToolbarViewModel.moc"
