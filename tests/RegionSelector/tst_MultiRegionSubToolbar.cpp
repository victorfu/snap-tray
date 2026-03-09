#include <QtTest>

#include "RegionSelector.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/RegionToolbarViewModel.h"

class tst_RegionSelectorMultiRegionSubToolbar : public QObject
{
    Q_OBJECT

private slots:
    void testToolbarToggleHidesSubToolbarInMultiRegionMode();
    void testBeginReplaceHidesStaleSubToolbar();
};

void tst_RegionSelectorMultiRegionSubToolbar::testToolbarToggleHidesSubToolbarInMultiRegionMode()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());

    selector.handleToolbarClick(ToolId::MultiRegion);

    QVERIFY(selector.m_inputState.multiRegionMode);
    QVERIFY(!selector.m_inputState.showSubToolbar);
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());

    const QVariantList buttons = selector.m_toolbarViewModel->buttons();
    QCOMPARE(buttons.size(), 2);
    QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("id")).toInt(),
             static_cast<int>(ToolId::MultiRegionDone));
    QCOMPARE(buttons.at(1).toMap().value(QStringLiteral("id")).toInt(),
             static_cast<int>(ToolId::Cancel));
}

void tst_RegionSelectorMultiRegionSubToolbar::testBeginReplaceHidesStaleSubToolbar()
{
    RegionSelector selector;

    selector.setMultiRegionMode(true);
    selector.m_multiRegionManager->addRegion(QRect(20, 30, 120, 90));

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());

    selector.beginMultiRegionReplace(0);

    QCOMPARE(selector.m_inputState.replaceTargetIndex, 0);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Selection);
    QVERIFY(!selector.m_inputState.showSubToolbar);
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());
}

QTEST_MAIN(tst_RegionSelectorMultiRegionSubToolbar)
#include "tst_MultiRegionSubToolbar.moc"
