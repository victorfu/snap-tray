#include <QtTest>
#include "region/SelectionStateManager.h"
#include <QPoint>
#include <QRect>
#include <QSignalSpy>

/**
 * @brief Test class for SelectionStateManager.
 *
 * Tests selection creation, resize, move operations,
 * and state transitions.
 */
class tst_SelectionStateManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Initial state tests
    void testInitialState();
    void testStateQueries_NoSelection();

    // Selection creation tests
    void testStartSelection();
    void testUpdateSelection();
    void testFinishSelection();
    void testFinishSelection_TooSmall();

    // Compound state queries
    void testHasSelection_Complete();
    void testHasSelection_Resizing();
    void testHasSelection_Moving();
    void testHasActiveSelection();

    // Resize operation tests
    void testHitTestHandle_TopLeft();
    void testHitTestHandle_BottomRight();
    void testHitTestHandle_None_Outside();
    void testStartResize();
    void testUpdateResize_TopLeft();
    void testUpdateResize_BottomRight();
    void testFinishResize();
    void testResize_MinimumSize();

    // Move operation tests
    void testHitTestMove_Inside();
    void testHitTestMove_Outside();
    void testStartMove();
    void testUpdateMove();
    void testFinishMove();
    void testMove_ClampToBounds();

    // Cancel/restore tests
    void testCancelResizeOrMove_Resize();
    void testCancelResizeOrMove_Move();

    // Window detection support
    void testSetFromDetectedWindow();

    // Clear selection
    void testClearSelection();

    // Signal tests
    void testStateChangedSignal();
    void testSelectionChangedSignal();

private:
    SelectionStateManager* m_manager;
};

void tst_SelectionStateManager::init()
{
    m_manager = new SelectionStateManager();
    m_manager->setBounds(QRect(0, 0, 1920, 1080));
}

void tst_SelectionStateManager::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

void tst_SelectionStateManager::testInitialState()
{
    QCOMPARE(m_manager->state(), SelectionStateManager::State::None);
    QCOMPARE(m_manager->selectionRect(), QRect());
}

void tst_SelectionStateManager::testStateQueries_NoSelection()
{
    QVERIFY(!m_manager->isComplete());
    QVERIFY(!m_manager->isSelecting());
    QVERIFY(!m_manager->isResizing());
    QVERIFY(!m_manager->isMoving());
    QVERIFY(!m_manager->hasSelection());
    QVERIFY(!m_manager->hasActiveSelection());
    QVERIFY(!m_manager->isManipulating());
}

void tst_SelectionStateManager::testStartSelection()
{
    m_manager->startSelection(QPoint(100, 100));

    QCOMPARE(m_manager->state(), SelectionStateManager::State::Selecting);
    QVERIFY(m_manager->isSelecting());
    QVERIFY(m_manager->hasActiveSelection());
}

void tst_SelectionStateManager::testUpdateSelection()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(200, 200));

    // QRect from points includes both endpoints, so 100 to 200 = 101 pixels
    QRect expected(100, 100, 101, 101);
    QCOMPARE(m_manager->selectionRect(), expected);
}

void tst_SelectionStateManager::testFinishSelection()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(200, 200));
    m_manager->finishSelection();

    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
    QVERIFY(m_manager->isComplete());
    QVERIFY(m_manager->hasSelection());
}

void tst_SelectionStateManager::testFinishSelection_TooSmall()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(102, 102));  // Only 2x2 pixels
    m_manager->finishSelection();

    // Selection should be cleared if too small
    QCOMPARE(m_manager->state(), SelectionStateManager::State::None);
    QVERIFY(!m_manager->hasSelection());
}

void tst_SelectionStateManager::testHasSelection_Complete()
{
    m_manager->setSelectionRect(QRect(100, 100, 200, 200));
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QVERIFY(m_manager->hasSelection());
}

void tst_SelectionStateManager::testHasSelection_Resizing()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);

    QVERIFY(m_manager->hasSelection());
    QVERIFY(m_manager->isManipulating());
}

void tst_SelectionStateManager::testHasSelection_Moving()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startMove(QPoint(200, 200));

    QVERIFY(m_manager->hasSelection());
    QVERIFY(m_manager->isManipulating());
}

void tst_SelectionStateManager::testHasActiveSelection()
{
    m_manager->startSelection(QPoint(100, 100));

    QVERIFY(m_manager->hasActiveSelection());
    QVERIFY(!m_manager->hasSelection());  // Not complete yet
}

void tst_SelectionStateManager::testHitTestHandle_TopLeft()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    auto handle = m_manager->hitTestHandle(QPoint(100, 100), 16);
    QCOMPARE(handle, SelectionStateManager::ResizeHandle::TopLeft);
}

void tst_SelectionStateManager::testHitTestHandle_BottomRight()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    auto handle = m_manager->hitTestHandle(QPoint(300, 300), 16);
    QCOMPARE(handle, SelectionStateManager::ResizeHandle::BottomRight);
}

void tst_SelectionStateManager::testHitTestHandle_None_Outside()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    auto handle = m_manager->hitTestHandle(QPoint(500, 500), 16);
    QCOMPARE(handle, SelectionStateManager::ResizeHandle::None);
}

void tst_SelectionStateManager::testStartResize()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);

    QCOMPARE(m_manager->state(), SelectionStateManager::State::ResizingHandle);
    QVERIFY(m_manager->isResizing());
}

void tst_SelectionStateManager::testUpdateResize_TopLeft()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);
    m_manager->updateResize(QPoint(50, 50));  // Move top-left up and left by 50

    // Original rect is 100,100 to 300,300 (201x201). Moving top-left to 50,50 makes it 251x251
    QRect expected(50, 50, 251, 251);
    QCOMPARE(m_manager->selectionRect(), expected);
}

void tst_SelectionStateManager::testUpdateResize_BottomRight()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startResize(QPoint(300, 300), SelectionStateManager::ResizeHandle::BottomRight);
    m_manager->updateResize(QPoint(400, 400));  // Expand bottom-right by 100

    // Original rect is 100,100 to 300,300. Expanding bottom-right to 400,400 makes it 301x301
    QRect expected(100, 100, 301, 301);
    QCOMPARE(m_manager->selectionRect(), expected);
}

void tst_SelectionStateManager::testFinishResize()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);
    m_manager->updateResize(QPoint(50, 50));
    m_manager->finishResize();

    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
    QVERIFY(!m_manager->isResizing());
}

void tst_SelectionStateManager::testResize_MinimumSize()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QRect originalRect = m_manager->selectionRect();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);
    // Try to make it too small (less than 10x10)
    m_manager->updateResize(QPoint(295, 295));

    // Selection should not change if it would become too small
    QCOMPARE(m_manager->selectionRect(), originalRect);
}

void tst_SelectionStateManager::testHitTestMove_Inside()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QVERIFY(m_manager->hitTestMove(QPoint(200, 200)));
}

void tst_SelectionStateManager::testHitTestMove_Outside()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QVERIFY(!m_manager->hitTestMove(QPoint(50, 50)));
}

void tst_SelectionStateManager::testStartMove()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startMove(QPoint(200, 200));

    QCOMPARE(m_manager->state(), SelectionStateManager::State::Moving);
    QVERIFY(m_manager->isMoving());
}

void tst_SelectionStateManager::testUpdateMove()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startMove(QPoint(200, 200));
    m_manager->updateMove(QPoint(250, 250));  // Move by (50, 50)

    // Original rect is 100,100 with size 201x201. After move by (50,50) it's at 150,150
    QRect expected(150, 150, 201, 201);
    QCOMPARE(m_manager->selectionRect(), expected);
}

void tst_SelectionStateManager::testFinishMove()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startMove(QPoint(200, 200));
    m_manager->updateMove(QPoint(250, 250));
    m_manager->finishMove();

    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
    QVERIFY(!m_manager->isMoving());
}

void tst_SelectionStateManager::testMove_ClampToBounds()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    m_manager->startMove(QPoint(200, 200));
    // Try to move outside bounds (left edge)
    m_manager->updateMove(QPoint(-100, 200));

    QRect rect = m_manager->selectionRect();
    QVERIFY(rect.left() >= 0);  // Should be clamped
}

void tst_SelectionStateManager::testCancelResizeOrMove_Resize()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QRect originalRect = m_manager->selectionRect();
    m_manager->startResize(QPoint(100, 100), SelectionStateManager::ResizeHandle::TopLeft);
    m_manager->updateResize(QPoint(50, 50));  // Change the rect
    m_manager->cancelResizeOrMove();

    QCOMPARE(m_manager->selectionRect(), originalRect);
    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
}

void tst_SelectionStateManager::testCancelResizeOrMove_Move()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    QRect originalRect = m_manager->selectionRect();
    m_manager->startMove(QPoint(200, 200));
    m_manager->updateMove(QPoint(300, 300));  // Change position
    m_manager->cancelResizeOrMove();

    QCOMPARE(m_manager->selectionRect(), originalRect);
    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
}

void tst_SelectionStateManager::testSetFromDetectedWindow()
{
    QRect windowRect(50, 50, 400, 300);
    m_manager->setFromDetectedWindow(windowRect);

    QCOMPARE(m_manager->selectionRect(), windowRect);
    QCOMPARE(m_manager->state(), SelectionStateManager::State::Complete);
}

void tst_SelectionStateManager::testClearSelection()
{
    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();

    m_manager->clearSelection();

    QCOMPARE(m_manager->state(), SelectionStateManager::State::None);
    QCOMPARE(m_manager->selectionRect(), QRect());
}

void tst_SelectionStateManager::testStateChangedSignal()
{
    QSignalSpy spy(m_manager, &SelectionStateManager::stateChanged);

    m_manager->startSelection(QPoint(100, 100));
    QCOMPARE(spy.count(), 1);

    m_manager->updateSelection(QPoint(300, 300));
    m_manager->finishSelection();
    QCOMPARE(spy.count(), 2);  // Selecting -> Complete
}

void tst_SelectionStateManager::testSelectionChangedSignal()
{
    QSignalSpy spy(m_manager, &SelectionStateManager::selectionChanged);

    m_manager->startSelection(QPoint(100, 100));
    m_manager->updateSelection(QPoint(200, 200));
    QCOMPARE(spy.count(), 1);

    m_manager->updateSelection(QPoint(300, 300));
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(tst_SelectionStateManager)
#include "tst_SelectionStateManager.moc"
