#include <QtTest>

#include <QCursor>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>

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
    void testReleaseAppliesFinalSelectionPoint_data();
    void testReleaseAppliesFinalSelectionPoint();
    void testReleaseAppliesFinalResizeAndMove();
    void testAspectLockedCornerResizeKeepsPressOffset_data();
    void testAspectLockedCornerResizeKeepsPressOffset();
    void testNoMoveKeepsDetectedWindowSelection();
    void testTinyMoveWithoutDetectionFallsBackToFullScreen();
    void testLargeDragUsesDragSelectionInsteadOfPendingWindow();
    void testDetectedWindowClickDefersSelectionUntilRealDrag();
    void testDetectedWindowRealDragClearsDetectionAndStartsSelection();
    void testDetectedWindowRealDragMarksSelectionTransition();
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

void tst_RegionInputHandler::testReleaseAppliesFinalSelectionPoint_data()
{
    QTest::addColumn<bool>("detected");
    QTest::addColumn<bool>("moved");
    QTest::addColumn<bool>("multi");
    QTest::addColumn<bool>("floating");
    for (int flags = 0; flags < 16; ++flags)
        QTest::newRow(qPrintable(QString::number(flags)))
            << bool(flags & 1) << bool(flags & 2) << bool(flags & 4) << bool(flags & 8);
}

void tst_RegionInputHandler::testReleaseAppliesFinalSelectionPoint()
{
    QFETCH(bool, detected);
    QFETCH(bool, moved);
    QFETCH(bool, multi);
    QFETCH(bool, floating);
    m_state.multiRegionMode = multi;
    m_state.hasDetectedWindow = detected;
    m_state.highlightedWindowRect = detected ? QRect(50, 50, 400, 300) : QRect();
    QSignalSpy finished(m_handler, &RegionInputHandler::selectionFinished);
    QSignalSpy fullscreen(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy cleared(m_handler, &RegionInputHandler::detectionCleared);
    auto press = makeMouseEvent(QEvent::MouseButtonPress, QPoint(100,100), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&press);
    if (moved) {
        auto move = makeMouseEvent(QEvent::MouseMove, QPoint(140,150), Qt::NoButton, Qt::LeftButton);
        m_handler->handleMouseMove(&move);
    }
    auto release = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(180,190), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&release, floating ? RegionInputHandler::ReleaseTarget::FloatingUi
                                                    : RegionInputHandler::ReleaseTarget::Canvas);
    QCOMPARE(m_selectionManager->selectionRect(), QRect(100,100,81,91));
    QCOMPARE(m_state.currentPoint, QPoint(180,190));
    QVERIFY(m_selectionManager->isComplete());
    QCOMPARE(finished.count(), 1);
    QCOMPARE(fullscreen.count(), 0);
    QCOMPARE(cleared.count(), detected ? 1 : 0);
    if (detected) QCOMPARE(cleared.first().at(1).toBool(), true);
}

void tst_RegionInputHandler::testReleaseAppliesFinalResizeAndMove()
{
    const QRect original(40,40,100,80);
    m_selectionManager->setSelectionRect(original);
    m_selectionManager->startResize(original.bottomRight(), SelectionStateManager::ResizeHandle::BottomRight);
    m_selectionManager->updateResize(QPoint(150,130));
    auto release = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(170,160), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&release, RegionInputHandler::ReleaseTarget::FloatingUi);
    QCOMPARE(m_selectionManager->selectionRect(), QRect(40,40,131,121));
    QVERIFY(m_selectionManager->isComplete());

    m_selectionManager->setSelectionRect(original);
    m_selectionManager->startMove(QPoint(80,80));
    m_selectionManager->updateMove(QPoint(90,90));
    auto moveRelease = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(110,120), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&moveRelease);
    QCOMPARE(m_selectionManager->selectionRect(), original.translated(30,40));
    QVERIFY(m_selectionManager->isComplete());
}

void tst_RegionInputHandler::testAspectLockedCornerResizeKeepsPressOffset_data()
{
    QTest::addColumn<QPoint>("pressPos");
    QTest::addColumn<QPoint>("releaseDelta");
    QTest::addColumn<QRect>("expected");
    const QRect original(100, 100, 100, 80);
    struct CornerCase {
        const char* name;
        QPoint pressPos;
        QPoint dragDelta;
        QRect resized;
    };
    const CornerCase cases[] = {
        {"top-left", QPoint(103, 103), QPoint(-20, -16), QRect(80, 84, 120, 96)},
        {"top-right", QPoint(196, 103), QPoint(20, -16), QRect(100, 84, 120, 96)},
        {"bottom-left", QPoint(103, 176), QPoint(-20, 16), QRect(80, 100, 120, 96)},
        {"bottom-right", QPoint(196, 176), QPoint(20, 16), QRect(100, 100, 120, 96)}
    };
    for (const auto& corner : cases) {
        QTest::addRow("%s-click", corner.name) << corner.pressPos << QPoint() << original;
        QTest::addRow("%s-drag", corner.name) << corner.pressPos << corner.dragDelta << corner.resized;
    }
}

void tst_RegionInputHandler::testAspectLockedCornerResizeKeepsPressOffset()
{
    QFETCH(QPoint, pressPos);
    QFETCH(QPoint, releaseDelta);
    QFETCH(QRect, expected);
    m_selectionManager->setSelectionRect(QRect(100, 100, 100, 80));
    m_selectionManager->setAspectRatio(1.25);
    auto press = makeMouseEvent(QEvent::MouseButtonPress, pressPos, Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&press);
    QVERIFY(m_selectionManager->isResizing());

    // No move event: release must apply the final delta without snapping to
    // the pointer's offset within the handle's hit area.
    auto release = makeMouseEvent(QEvent::MouseButtonRelease, pressPos + releaseDelta,
                                  Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&release);
    QCOMPARE(m_selectionManager->selectionRect(), expected);
    QVERIFY(m_selectionManager->isComplete());
}

void tst_RegionInputHandler::testTinyMoveKeepsDetectedWindowSelection()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);
    QSignalSpy detectionClearedSpy(m_handler, &RegionInputHandler::detectionCleared);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(163, 182), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(163, 182), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QCOMPARE(detectionClearedSpy.count(), 1);
    const auto clickClearArgs = detectionClearedSpy.takeFirst();
    QCOMPARE(clickClearArgs.at(0).toRect(), detectedWindow);
    QCOMPARE(clickClearArgs.at(1).toBool(), false);
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
    const auto dragClearArgs = detectionClearedSpy.takeFirst();
    QCOMPARE(dragClearArgs.at(0).toRect(), detectedWindow);
    QCOMPARE(dragClearArgs.at(1).toBool(), true);
}

void tst_RegionInputHandler::testDetectedWindowRealDragMarksSelectionTransition()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy detectionClearedSpy(m_handler, &RegionInputHandler::detectionCleared);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(210, 230), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QCOMPARE(detectionClearedSpy.count(), 1);
    const auto clearArgs = detectionClearedSpy.takeFirst();
    QCOMPARE(clearArgs.at(0).toRect(), detectedWindow);
    QCOMPARE(clearArgs.at(1).toBool(), true);
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
