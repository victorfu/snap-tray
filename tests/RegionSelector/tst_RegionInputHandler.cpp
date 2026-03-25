#include <QtTest>

#include <QMouseEvent>
#include <QSignalSpy>
#include <QWidget>

#include "annotations/AnnotationLayer.h"
#include "cursor/CursorManager.h"
#include "region/RegionInputHandler.h"
#include "region/RegionInputState.h"
#include "region/SelectionStateManager.h"
#include "region/UpdateThrottler.h"

namespace {

QMouseEvent makeMouseEvent(QEvent::Type type,
                           const QPoint& pos,
                           Qt::MouseButton button,
                           Qt::MouseButtons buttons)
{
    const QPointF point(pos);
    return QMouseEvent(type, point, point, point, button, buttons, Qt::NoModifier);
}

} // namespace

class tst_RegionInputHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTinyMoveKeepsDetectedWindowSelection();
    void testNoMoveKeepsDetectedWindowSelection();
    void testTinyMoveWithoutDetectionFallsBackToFullScreen();
    void testLargeDragUsesDragSelectionInsteadOfPendingWindow();
    void testDetectedWindowClickDefersSelectionUntilRealDrag();
    void testDetectedWindowRealDragClearsDetectionAndStartsSelection();
    void testDetectedWindowRealDragRequestsHostRepaint();
    void testSelectionMoveSetsAndClearsDragStateOnRelease();
    void testSelectionMoveClearsDragStateOnRightClickCancel();
    void testMouseMoveEmitsCurrentPointUpdatedDuringSelectionDrag();
    void testCompletedSelectionHoverSkipsMagnifierDirtyRegionWhenDisabled();

private:
    RegionInputHandler* m_handler = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    QWidget* m_parentWidget = nullptr;
    RegionInputState m_state;
};

void tst_RegionInputHandler::init()
{
    m_handler = new RegionInputHandler();
    m_selectionManager = new SelectionStateManager();
    m_annotationLayer = new AnnotationLayer();
    m_parentWidget = new QWidget();
    m_selectionManager->setBounds(QRect(0, 0, 1920, 1080));
    CursorManager::instance().registerWidget(m_parentWidget);

    m_state = RegionInputState();
    m_handler->setSelectionManager(m_selectionManager);
    m_handler->setAnnotationLayer(m_annotationLayer);
    m_handler->setParentWidget(m_parentWidget);
    m_handler->setSharedState(&m_state);
}

void tst_RegionInputHandler::cleanup()
{
    CursorManager::instance().unregisterWidget(m_parentWidget);

    delete m_parentWidget;
    m_parentWidget = nullptr;

    delete m_annotationLayer;
    m_annotationLayer = nullptr;

    delete m_selectionManager;
    m_selectionManager = nullptr;

    delete m_handler;
    m_handler = nullptr;
}

void tst_RegionInputHandler::testTinyMoveKeepsDetectedWindowSelection()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(163, 182), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(163, 182), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());
    QCOMPARE(m_selectionManager->selectionRect(), detectedWindow);
}

void tst_RegionInputHandler::testNoMoveKeepsDetectedWindowSelection()
{
    const QRect detectedWindow(40, 60, 280, 190);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(90, 90), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(90, 90), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());
    QCOMPARE(m_selectionManager->selectionRect(), detectedWindow);
}

void tst_RegionInputHandler::testTinyMoveWithoutDetectionFallsBackToFullScreen()
{
    m_state.hasDetectedWindow = false;
    m_state.highlightedWindowRect = QRect();

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(200, 220), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(203, 223), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(203, 223), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 1);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(!m_selectionManager->isComplete());
}

void tst_RegionInputHandler::testLargeDragUsesDragSelectionInsteadOfPendingWindow()
{
    const QRect detectedWindow(500, 520, 180, 140);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(100, 110), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(145, 155), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(145, 155), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());

    const QRect selectedRect = m_selectionManager->selectionRect();
    QVERIFY(selectedRect.width() > 5);
    QVERIFY(selectedRect.height() > 5);
    QVERIFY(selectedRect != detectedWindow);
}

void tst_RegionInputHandler::testDetectedWindowClickDefersSelectionUntilRealDrag()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy detectionClearedSpy(m_handler, &RegionInputHandler::detectionCleared);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    QVERIFY(!m_selectionManager->hasActiveSelection());
    QCOMPARE(detectionClearedSpy.count(), 0);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(163, 182), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QVERIFY(!m_selectionManager->hasActiveSelection());
    QCOMPARE(detectionClearedSpy.count(), 0);
}

void tst_RegionInputHandler::testDetectedWindowRealDragClearsDetectionAndStartsSelection()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy detectionClearedSpy(m_handler, &RegionInputHandler::detectionCleared);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(210, 230), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QVERIFY(m_selectionManager->isSelecting());
    QCOMPARE(detectionClearedSpy.count(), 1);
    QCOMPARE(detectionClearedSpy.takeFirst().at(0).toRect(), detectedWindow);
}

void tst_RegionInputHandler::testDetectedWindowRealDragRequestsHostRepaint()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy updateSpy(m_handler, qOverload<>(&RegionInputHandler::updateRequested));

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(210, 230), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QVERIFY(updateSpy.count() >= 1);
}

void tst_RegionInputHandler::testSelectionMoveSetsAndClearsDragStateOnRelease()
{
    m_state.currentTool = ToolId::Selection;
    m_selectionManager->setSelectionRect(QRect(100, 120, 200, 160));
    QVERIFY(m_selectionManager->isComplete());

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    QCOMPARE(m_selectionManager->state(), SelectionStateManager::State::Moving);
    QCOMPARE(CursorManager::instance().dragStateForWidget(m_parentWidget), DragState::SelectionDrag);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(160, 180), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(m_selectionManager->state(), SelectionStateManager::State::Complete);
    QCOMPARE(CursorManager::instance().dragStateForWidget(m_parentWidget), DragState::None);
}

void tst_RegionInputHandler::testSelectionMoveClearsDragStateOnRightClickCancel()
{
    m_state.currentTool = ToolId::Selection;
    m_selectionManager->setSelectionRect(QRect(100, 120, 200, 160));
    QVERIFY(m_selectionManager->isComplete());

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    QCOMPARE(m_selectionManager->state(), SelectionStateManager::State::Moving);
    QCOMPARE(CursorManager::instance().dragStateForWidget(m_parentWidget), DragState::SelectionDrag);

    auto cancelEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::RightButton, Qt::RightButton);
    m_handler->handleMousePress(&cancelEvent);

    QCOMPARE(m_selectionManager->state(), SelectionStateManager::State::None);
    QCOMPARE(CursorManager::instance().dragStateForWidget(m_parentWidget), DragState::None);
}

void tst_RegionInputHandler::testMouseMoveEmitsCurrentPointUpdatedDuringSelectionDrag()
{
    QSignalSpy pointSpy(m_handler, &RegionInputHandler::currentPointUpdated);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(120, 140), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(180, 210), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QCOMPARE(pointSpy.count(), 1);
    QCOMPARE(pointSpy.takeFirst().at(0).toPoint(), QPoint(180, 210));
}

void tst_RegionInputHandler::testCompletedSelectionHoverSkipsMagnifierDirtyRegionWhenDisabled()
{
    UpdateThrottler throttler;
    throttler.startAll();
    m_handler->setUpdateThrottler(&throttler);
    m_handler->setMagnifierVisibilityProvider([]() { return false; });
    m_handler->resetDirtyTracking();

    m_selectionManager->setSelectionRect(QRect(100, 120, 200, 160));
    QVERIFY(m_selectionManager->isComplete());

    QTest::qWait(UpdateThrottler::kHoverMs + 5);

    auto firstMoveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(160, 180), Qt::NoButton, Qt::NoButton);
    m_handler->handleMouseMove(&firstMoveEvent);

    QTest::qWait(UpdateThrottler::kHoverMs + 5);

    auto secondMoveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(164, 184), Qt::NoButton, Qt::NoButton);
    m_handler->handleMouseMove(&secondMoveEvent);

    QVERIFY(m_handler->lastMagnifierRect().isNull());
}

QTEST_MAIN(tst_RegionInputHandler)
#include "tst_RegionInputHandler.moc"
