#include <QtTest>
#include <QWheelEvent>

#include "RegionSelector.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/RegionToolbarViewModel.h"

class tst_RegionSelectorMultiRegionSubToolbar : public QObject
{
    Q_OBJECT

private slots:
    void testToolbarToggleHidesSubToolbarInMultiRegionMode();
    void testBeginReplaceHidesStaleSubToolbar();
    void testCompletedSelectionReopensHiddenSubToolbar();
    void testWheelDoesNotAdjustWidthWithoutCompletedSelection();
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

void tst_RegionSelectorMultiRegionSubToolbar::testCompletedSelectionReopensHiddenSubToolbar()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_qmlSubToolbar->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(selector.m_qmlSubToolbar->isVisible());

    selector.m_toolOptionsViewModel->clearSections();
    selector.m_qmlSubToolbar->hide();
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(!selector.m_qmlSubToolbar->isVisible());

    selector.syncRegionSubToolbar(false);

    QVERIFY(selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(selector.m_qmlSubToolbar->isVisible());
}

void tst_RegionSelectorMultiRegionSubToolbar::testWheelDoesNotAdjustWidthWithoutCompletedSelection()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    selector.m_toolOptionsViewModel->setCurrentWidth(5);
    QVERIFY(selector.m_toolOptionsViewModel->showWidthSection());
    QVERIFY(!selector.m_selectionManager->isComplete());

    QWheelEvent event(QPointF(12, 12),
                      QPointF(12, 12),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);

    selector.wheelEvent(&event);

    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 5);
}

QTEST_MAIN(tst_RegionSelectorMultiRegionSubToolbar)
#include "tst_MultiRegionSubToolbar.moc"
