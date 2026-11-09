#include <QtTest/QtTest>
#include "tools/ToolbarTraits.h"

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
    void buttonsCarryGroups();
    void collapsePriorityOrder();
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


void tst_RegionToolbarViewModel::buttonsCarryGroups()
{
    RegionToolbarViewModel vm;
    QHash<int, int> groupById;
    int previousGroup = -1;
    for (const QVariant& v : vm.buttons()) {
        const QVariantMap b = v.toMap();
        const int group = b.value("group").toInt();
        groupById.insert(b.value("id").toInt(), group);
        QCOMPARE(b.value("groupStart").toBool(), previousGroup != -1 && group != previousGroup);
        if (b.value("groupStart").toBool()) QVERIFY(b.value("separatorBefore").toBool());
        QVERIFY(group >= previousGroup); // groups are contiguous and ordered
        previousGroup = group;
    }
    QCOMPARE(groupById.value(int(ToolId::Arrow)), int(ToolbarGroup::Draw));
    QCOMPARE(groupById.value(int(ToolId::EmojiSticker)), int(ToolbarGroup::Draw));
    QCOMPARE(groupById.value(int(ToolId::Undo)), int(ToolbarGroup::Assist));
    QCOMPARE(groupById.value(int(ToolId::MultiRegion)), int(ToolbarGroup::Assist));
    QCOMPARE(groupById.value(int(ToolId::Pin)), int(ToolbarGroup::Finish));
    QCOMPARE(groupById.value(int(ToolId::Copy)), int(ToolbarGroup::Finish));
}

void tst_RegionToolbarViewModel::collapsePriorityOrder()
{
    QVERIFY(toolbarCollapsePriority(ToolId::QRCode) < toolbarCollapsePriority(ToolId::OCR));
    QVERIFY(toolbarCollapsePriority(ToolId::OCR) < toolbarCollapsePriority(ToolId::MultiRegion));
    QVERIFY(toolbarCollapsePriority(ToolId::MultiRegion) < toolbarCollapsePriority(ToolId::EmojiSticker));
    QVERIFY(toolbarCollapsePriority(ToolId::EmojiSticker) < toolbarCollapsePriority(ToolId::StepBadge));
    QVERIFY(toolbarCollapsePriority(ToolId::StepBadge) < toolbarCollapsePriority(ToolId::Eraser));
    QVERIFY(toolbarCollapsePriority(ToolId::Eraser) < toolbarCollapsePriority(ToolId::Mosaic));
    QVERIFY(toolbarCollapsePriority(ToolId::Mosaic) < toolbarCollapsePriority(ToolId::Text));
    for (ToolId id : {ToolId::Copy, ToolId::Save, ToolId::Pin, ToolId::Undo, ToolId::Redo,
                      ToolId::Cancel, ToolId::Exit, ToolId::MultiRegionDone})
        QCOMPARE(toolbarCollapsePriority(id), kNeverCollapse);
}

QTEST_MAIN(tst_RegionToolbarViewModel)
#include "tst_RegionToolbarViewModel.moc"
