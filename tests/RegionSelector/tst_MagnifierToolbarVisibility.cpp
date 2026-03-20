#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "region/MagnifierOverlay.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlOverlayPanel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

class VisibilityEventCounter : public QObject
{
public:
    int showCount = 0;
    int hideCount = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (event->type() == QEvent::Show) {
            ++showCount;
        }
        else if (event->type() == QEvent::Hide) {
            ++hideCount;
        }
        return false;
    }
};

} // namespace

class tst_RegionSelectorMagnifierToolbarVisibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSelectionToolbarSuppressesMagnifier();
    void testShiftToggleRequiresVisibleMagnifier();
    void testHideSelectionFloatingUiDoesNotHideMagnifierOverlay();
    void testCompletedSelectionDragSuppressesFloatingUi();
    void testCompletedSelectionDragRestoresFloatingUi();
    void testCompletedSelectionDragRestoresMagnifierEligibility();
    void testCompletedSelectionDragClearsManualToolbarPlacement();
    void testMultiRegionCompletedSelectionDragSuppressesFloatingUi();

private:
    static bool positionCursorForVisibleMagnifier(RegionSelector& selector, const QRect& selectionRect);
};

void tst_RegionSelectorMagnifierToolbarVisibility::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector floating UI tests in this environment.");
    }
}

bool tst_RegionSelectorMagnifierToolbarVisibility::positionCursorForVisibleMagnifier(
    RegionSelector& selector, const QRect& selectionRect)
{
    const QRect normalized = selectionRect.normalized();
    const QList<QPoint> candidates = {
        normalized.center(),
        normalized.topLeft() + QPoint(24, 24),
        normalized.topRight() + QPoint(-24, 24),
        normalized.bottomLeft() + QPoint(24, -24),
        normalized.bottomRight() + QPoint(-24, -24)
    };

    for (const QPoint& localPoint : candidates) {
        if (!normalized.contains(localPoint)) {
            continue;
        }

        const QPoint globalPoint = selector.mapToGlobal(localPoint);
        if (selector.isGlobalPosOverFloatingUi(globalPoint)) {
            continue;
        }

        selector.m_inputState.currentPoint = localPoint;
        QCursor::setPos(globalPoint);
        QCoreApplication::processEvents();
        if (selector.shouldShowMagnifier()) {
            return true;
        }
    }

    return false;
}

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

void tst_RegionSelectorMagnifierToolbarVisibility::testHideSelectionFloatingUiDoesNotHideMagnifierOverlay()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.m_inputState.currentTool = ToolId::Selection;
    selector.m_inputState.showSubToolbar = false;
    selector.m_inputState.isDrawing = false;
    selector.m_inputState.currentPoint = QPoint(180, 140);
    selector.m_cursorOverSelectionToolbar = false;

    const QPoint globalPoint = selector.mapToGlobal(selector.m_inputState.currentPoint);
    QCursor::setPos(globalPoint);
    QCoreApplication::processEvents();

    QVERIFY(selector.shouldShowMagnifier());
    selector.syncMagnifierOverlay();
    QTRY_VERIFY(selector.m_magnifierOverlay->isVisible());

    VisibilityEventCounter counter;
    selector.m_magnifierOverlay->installEventFilter(&counter);

    selector.hideSelectionFloatingUi(false);
    QCoreApplication::processEvents();

    QCOMPARE(counter.hideCount, 0);
    QVERIFY(selector.m_magnifierOverlay->isVisible());

    selector.m_magnifierOverlay->removeEventFilter(&counter);
}

void tst_RegionSelectorMagnifierToolbarVisibility::testCompletedSelectionDragSuppressesFloatingUi()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_inputState.isDrawing = false;
    selector.m_inputState.currentPoint = QPoint(140, 110);
    selector.m_selectionManager->setSelectionRect(QRect(80, 70, 240, 150));
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    selector.m_qmlToolbar->show();
    selector.m_qmlSubToolbar->showForTool(static_cast<int>(ToolId::Arrow));
    selector.m_regionControlPanel->show();
    QCoreApplication::processEvents();

    QVERIFY(selector.m_selectionManager->isComplete());
    QTRY_VERIFY(selector.m_qmlToolbar->isVisible());
    QTRY_VERIFY(selector.m_qmlSubToolbar->isVisible());
    QTRY_VERIFY(selector.m_regionControlPanel->isVisible());

    selector.m_selectionManager->startMove(QPoint(120, 90));

    QVERIFY(selector.completedSelectionDragUiSuppressed());
    QVERIFY(!selector.shouldShowMagnifier());
    QTRY_VERIFY(!selector.m_qmlToolbar->isVisible());
    QTRY_VERIFY(!selector.m_qmlSubToolbar->isVisible());
    QTRY_VERIFY(!selector.m_regionControlPanel->isVisible());
}

void tst_RegionSelectorMagnifierToolbarVisibility::testCompletedSelectionDragRestoresFloatingUi()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_inputState.isDrawing = false;
    selector.m_inputState.currentPoint = QPoint(160, 120);
    selector.m_selectionManager->setSelectionRect(QRect(90, 80, 260, 160));
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));

    selector.m_selectionManager->startResize(QPoint(90, 80),
        SelectionStateManager::ResizeHandle::TopLeft);
    QVERIFY(selector.completedSelectionDragUiSuppressed());

    selector.m_selectionManager->finishResize();
    QVERIFY(!selector.completedSelectionDragUiSuppressed());

    selector.update();
    QCoreApplication::processEvents();

    QTRY_VERIFY(selector.m_qmlToolbar->isVisible());
    QTRY_VERIFY(selector.m_qmlSubToolbar->isVisible());
    QTRY_VERIFY(selector.m_regionControlPanel->isVisible());
}

void tst_RegionSelectorMagnifierToolbarVisibility::testCompletedSelectionDragRestoresMagnifierEligibility()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.m_inputState.currentTool = ToolId::Selection;
    selector.m_inputState.showSubToolbar = false;
    selector.m_inputState.isDrawing = false;
    selector.m_selectionManager->setSelectionRect(QRect(90, 80, 260, 160));

    selector.m_selectionManager->startResize(QPoint(90, 80),
        SelectionStateManager::ResizeHandle::TopLeft);
    QVERIFY(selector.completedSelectionDragUiSuppressed());
    QVERIFY(!selector.shouldShowMagnifier());

    selector.m_selectionManager->finishResize();
    QVERIFY(!selector.completedSelectionDragUiSuppressed());

    selector.hideSelectionFloatingUi(true);
    selector.m_cursorOverSelectionToolbar = false;
    QCoreApplication::processEvents();

    QVERIFY(positionCursorForVisibleMagnifier(selector, selector.m_selectionManager->selectionRect()));
}

void tst_RegionSelectorMagnifierToolbarVisibility::testCompletedSelectionDragClearsManualToolbarPlacement()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_inputState.isDrawing = false;
    selector.m_selectionManager->setSelectionRect(QRect(90, 80, 260, 160));
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));

    selector.update();
    QCoreApplication::processEvents();

    QTRY_VERIFY(selector.m_qmlToolbar->isVisible());

    const QPoint manualPos = selector.mapToGlobal(QPoint(12, 12));
    selector.m_qmlToolbar->setPosition(manualPos);
    selector.m_toolbarUserDragged = true;
    const QRect manualGeometry = selector.m_qmlToolbar->geometry();

    selector.m_selectionManager->startMove(QPoint(160, 120));
    selector.m_selectionManager->updateMove(QPoint(260, 200));

    QVERIFY(selector.completedSelectionDragUiSuppressed());
    QVERIFY(!selector.m_toolbarUserDragged);

    selector.m_selectionManager->finishMove();
    selector.update();
    QCoreApplication::processEvents();

    QTRY_VERIFY(selector.m_qmlToolbar->isVisible());
    QVERIFY(!selector.m_toolbarUserDragged);
    QVERIFY(selector.m_qmlToolbar->geometry() != manualGeometry);
}

void tst_RegionSelectorMagnifierToolbarVisibility::testMultiRegionCompletedSelectionDragSuppressesFloatingUi()
{
    RegionSelector selector;
    selector.resize(800, 600);
    selector.show();
    QCoreApplication::processEvents();

    selector.setMultiRegionMode(true);
    selector.m_multiRegionManager->addRegion(QRect(100, 90, 220, 140));
    selector.m_multiRegionManager->setActiveIndex(0);
    selector.m_selectionManager->setSelectionRect(QRect(100, 90, 220, 140));
    selector.m_inputState.currentPoint = QPoint(160, 130);

    QVERIFY(selector.m_selectionManager->isComplete());
    QVERIFY(!selector.completedSelectionDragUiSuppressed());

    selector.m_selectionManager->startMove(QPoint(160, 130));

    QVERIFY(selector.completedSelectionDragUiSuppressed());
    QVERIFY(!selector.shouldShowMagnifier());
}

QTEST_MAIN(tst_RegionSelectorMagnifierToolbarVisibility)
#include "tst_MagnifierToolbarVisibility.moc"
