#include <QtTest/QtTest>

#include <QKeyEvent>

#include "RegionSelector.h"

class tst_RegionSelectorMagnifierToolbarVisibility : public QObject
{
    Q_OBJECT

private slots:
    void testSelectionToolbarSuppressesMagnifier();
    void testShiftToggleRequiresVisibleMagnifier();
};

void tst_RegionSelectorMagnifierToolbarVisibility::testSelectionToolbarSuppressesMagnifier()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Selection;
    selector.m_inputState.isDrawing = false;
    selector.m_inputState.currentPoint = QPoint(120, 90);
    selector.m_selectionManager->setSelectionRect(QRect(60, 50, 220, 140));

    QVERIFY(selector.m_selectionManager->isComplete());

    selector.m_cursorOverSelectionToolbar = false;
    QVERIFY(selector.shouldShowMagnifier());

    selector.m_cursorOverSelectionToolbar = true;
    QVERIFY(!selector.shouldShowMagnifier());
}

void tst_RegionSelectorMagnifierToolbarVisibility::testShiftToggleRequiresVisibleMagnifier()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Selection;
    selector.m_inputState.isDrawing = false;
    selector.m_inputState.currentPoint = QPoint(140, 110);
    selector.m_selectionManager->setSelectionRect(QRect(80, 70, 240, 150));

    QVERIFY(selector.m_selectionManager->isComplete());
    QCOMPARE(selector.m_magnifierPanel->showHexColor(), false);

    QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);

    selector.m_cursorOverSelectionToolbar = false;
    selector.keyPressEvent(&shiftPress);
    QCOMPARE(selector.m_magnifierPanel->showHexColor(), true);

    selector.m_magnifierPanel->setShowHexColor(false);
    selector.m_cursorOverSelectionToolbar = true;
    selector.keyPressEvent(&shiftPress);
    QCOMPARE(selector.m_magnifierPanel->showHexColor(), false);
}

QTEST_MAIN(tst_RegionSelectorMagnifierToolbarVisibility)
#include "tst_MagnifierToolbarVisibility.moc"
