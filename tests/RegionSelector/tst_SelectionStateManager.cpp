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
    void testSelectionBoundsInEveryDirection_data();
    void testSelectionBoundsInEveryDirection();
    void testAllResizeHandlesClampOnlyMovedEdges_data();
    void testAllResizeHandlesClampOnlyMovedEdges();
    void testBoundedRatioCornersAndUnboundedRestore();

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
    void testKeyboardEdgeResize_ClampsToBounds_data();
    void testKeyboardEdgeResize_ClampsToBounds();
    void testAspectRatioEdgeResize_ClampsToBounds_data();
    void testAspectRatioEdgeResize_ClampsToBounds();

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

void tst_SelectionStateManager::testSelectionBoundsInEveryDirection_data()
{
    QTest::addColumn<QPoint>("end");
    QTest::addColumn<qreal>("ratio");
    for (QPoint end : {QPoint(-500, -500), QPoint(500, -500), QPoint(-500, 500), QPoint(500, 500)})
        for (qreal ratio : {0.0, 1.0, 16.0 / 9.0})
            QTest::newRow(qPrintable(QString("%1-%2-ratio-%3").arg(end.x()).arg(end.y()).arg(ratio))) << end << ratio;
}

void tst_SelectionStateManager::testSelectionBoundsInEveryDirection()
{
    QFETCH(QPoint, end);
    QFETCH(qreal, ratio);
    const QRect bounds(10, 20, 200, 120);
    m_manager->setBounds(bounds);
    m_manager->setAspectRatio(ratio);
    m_manager->startSelection(QPoint(90, 70));
    m_manager->updateSelection(end);
    m_manager->finishSelection();
    const QRect rect = m_manager->selectionRect();
    QVERIFY(bounds.contains(rect));
    QVERIFY(rect.width() >= 5 && rect.height() >= 5);
    if (ratio > 0)
        QVERIFY(qAbs(rect.width() - ratio * rect.height()) <= ratio + 1);
    m_manager->clearSelection();
    m_manager->setAspectRatio(0);
    m_manager->startSelection(QPoint(-100, 70));
    QCOMPARE(m_manager->selectionRect().topLeft(), QPoint(10, 70));
}

void tst_SelectionStateManager::testAllResizeHandlesClampOnlyMovedEdges_data()
{
    QTest::addColumn<int>("handle");
    QTest::addColumn<QPoint>("press");
    QTest::addColumn<QPoint>("end");
    QTest::addColumn<QRect>("expected");
    using H = SelectionStateManager::ResizeHandle;
    QTest::newRow("top-left") << int(H::TopLeft) << QPoint(60,60) << QPoint(-500,-500) << QRect(10,20,130,80);
    QTest::newRow("top") << int(H::Top) << QPoint(100,60) << QPoint(100,-500) << QRect(60,20,80,80);
    QTest::newRow("top-right") << int(H::TopRight) << QPoint(139,60) << QPoint(500,-500) << QRect(60,20,150,80);
    QTest::newRow("left") << int(H::Left) << QPoint(60,80) << QPoint(-500,80) << QRect(10,60,130,40);
    QTest::newRow("right") << int(H::Right) << QPoint(139,80) << QPoint(500,80) << QRect(60,60,150,40);
    QTest::newRow("bottom-left") << int(H::BottomLeft) << QPoint(60,99) << QPoint(-500,500) << QRect(10,60,130,80);
    QTest::newRow("bottom") << int(H::Bottom) << QPoint(100,99) << QPoint(100,500) << QRect(60,60,80,80);
    QTest::newRow("bottom-right") << int(H::BottomRight) << QPoint(139,99) << QPoint(500,500) << QRect(60,60,150,80);
    QTest::newRow("flip") << int(H::Right) << QPoint(139,80) << QPoint(-500,80) << QRect(11,60,49,40);
    QTest::newRow("minimum") << int(H::Right) << QPoint(139,80) << QPoint(63,80) << QRect(60,60,80,40);
}

void tst_SelectionStateManager::testAllResizeHandlesClampOnlyMovedEdges()
{
    QFETCH(int, handle);
    QFETCH(QPoint, press);
    QFETCH(QPoint, end);
    QFETCH(QRect, expected);
    m_manager->setBounds(QRect(10,20,200,120));
    m_manager->setSelectionRect(QRect(60,60,80,40));
    m_manager->startResize(press, static_cast<SelectionStateManager::ResizeHandle>(handle));
    m_manager->updateResize(end);
    m_manager->finishResize();
    QCOMPARE(m_manager->selectionRect(), expected);
}

void tst_SelectionStateManager::testBoundedRatioCornersAndUnboundedRestore()
{
    const QRect bounds(10,20,200,120);
    using H = SelectionStateManager::ResizeHandle;
    const H handles[] = {H::TopLeft, H::TopRight, H::BottomLeft, H::BottomRight};
    const QPoint presses[] = {{60,60},{139,60},{60,99},{139,99}};
    const QPoint ends[] = {{-500,-500},{500,-500},{-500,500},{500,500}};
    const QPoint anchors[] = {{139,99},{60,99},{139,60},{60,60}};
    for (int i = 0; i < 4; ++i) {
        m_manager->setBounds(bounds);
        m_manager->setAspectRatio(16.0/9.0);
        m_manager->setSelectionRect(QRect(60,60,80,40));
        m_manager->startResize(presses[i], handles[i]);
        m_manager->updateResize(ends[i]);
        const QRect resized = m_manager->selectionRect();
        QVERIFY(bounds.contains(resized));
        QVERIFY(qAbs(resized.width() - (16.0/9.0) * resized.height()) < 3);
        const QPoint fixed = i == 0 ? resized.bottomRight() : i == 1 ? resized.bottomLeft()
            : i == 2 ? resized.topRight() : resized.topLeft();
        QCOMPARE(fixed, anchors[i]);
        m_manager->updateResize(anchors[i]);
        QCOMPARE(m_manager->selectionRect(), resized);
        m_manager->updateResize(ends[3 - i]);
        QVERIFY(bounds.contains(m_manager->selectionRect()));
        QVERIFY(m_manager->selectionRect().contains(anchors[i]));
        m_manager->finishResize();
    }
    const QRect restored(-30, -40, 500, 300);
    m_manager->setSelectionRect(restored);
    QCOMPARE(m_manager->selectionRect(), restored);
    m_manager->setBounds({});
    m_manager->setAspectRatio(0);
    m_manager->startSelection(QPoint(-30, -40));
    m_manager->updateSelection(QPoint(500, 300));
    QCOMPARE(m_manager->selectionRect(), QRect(-30, -40, 531, 341));
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

void tst_SelectionStateManager::testKeyboardEdgeResize_ClampsToBounds_data()
{
    QTest::addColumn<QRect>("selection");
    QTest::addColumn<QPoint>("delta");
    QTest::addColumn<QRect>("expected");
    QTest::addColumn<bool>("changed");

    QTest::newRow("grow-right-to-boundary")
        << QRect(10, 10, 89, 60) << QPoint(1, 0)
        << QRect(10, 10, 90, 60) << true;
    QTest::newRow("grow-right-at-boundary")
        << QRect(10, 10, 90, 60) << QPoint(1, 0)
        << QRect(10, 10, 90, 60) << false;
    QTest::newRow("grow-bottom-to-boundary")
        << QRect(10, 10, 90, 69) << QPoint(0, 1)
        << QRect(10, 10, 90, 70) << true;
    QTest::newRow("grow-bottom-at-boundary")
        << QRect(10, 10, 90, 70) << QPoint(0, 1)
        << QRect(10, 10, 90, 70) << false;
    QTest::newRow("minimum-width")
        << QRect(10, 10, 10, 30) << QPoint(-1, 0)
        << QRect(10, 10, 10, 30) << false;
    QTest::newRow("minimum-height")
        << QRect(10, 10, 30, 10) << QPoint(0, -1)
        << QRect(10, 10, 30, 10) << false;
    QTest::newRow("shrink-inside-bounds")
        << QRect(10, 10, 30, 30) << QPoint(-1, 0)
        << QRect(10, 10, 29, 30) << true;
}

void tst_SelectionStateManager::testKeyboardEdgeResize_ClampsToBounds()
{
    QFETCH(QRect, selection);
    QFETCH(QPoint, delta);
    QFETCH(QRect, expected);
    QFETCH(bool, changed);

    const QRect bounds(0, 0, 100, 80);
    m_manager->setBounds(bounds);
    m_manager->setSelectionRect(selection);
    QSignalSpy selectionSpy(m_manager, &SelectionStateManager::selectionChanged);

    QCOMPARE(m_manager->resizeFromBottomRight(delta), changed);
    QCOMPARE(m_manager->selectionRect(), expected);
    QVERIFY(bounds.contains(m_manager->selectionRect()));
    QCOMPARE(selectionSpy.count(), changed ? 1 : 0);
}

void tst_SelectionStateManager::testAspectRatioEdgeResize_ClampsToBounds_data()
{
    QTest::addColumn<int>("handleValue");
    QTest::addColumn<QRect>("originalRect");
    QTest::addColumn<QPoint>("pressPos");
    QTest::addColumn<QPoint>("dragPos");

    using Handle = SelectionStateManager::ResizeHandle;
    QTest::newRow("top-edge-width-limited")
        << static_cast<int>(Handle::Top)
        << QRect(20, 100, 160, 90)
        << QPoint(99, 100)
        << QPoint(99, -200);
    QTest::newRow("bottom-edge-width-limited")
        << static_cast<int>(Handle::Bottom)
        << QRect(220, 100, 160, 90)
        << QPoint(299, 189)
        << QPoint(299, 500);
    QTest::newRow("left-edge-height-limited")
        << static_cast<int>(Handle::Left)
        << QRect(100, 20, 160, 90)
        << QPoint(100, 64)
        << QPoint(-200, 64);
    QTest::newRow("right-edge-height-limited")
        << static_cast<int>(Handle::Right)
        << QRect(100, 190, 160, 90)
        << QPoint(259, 234)
        << QPoint(500, 234);
}

void tst_SelectionStateManager::testAspectRatioEdgeResize_ClampsToBounds()
{
    QFETCH(int, handleValue);
    QFETCH(QRect, originalRect);
    QFETCH(QPoint, pressPos);
    QFETCH(QPoint, dragPos);

    using Handle = SelectionStateManager::ResizeHandle;
    const auto handle = static_cast<Handle>(handleValue);
    const QRect bounds(0, 0, 400, 300);
    const qreal ratio = static_cast<qreal>(originalRect.width()) / originalRect.height();

    m_manager->setBounds(bounds);
    m_manager->setSelectionRect(originalRect);
    m_manager->setAspectRatio(ratio);
    m_manager->startResize(pressPos, handle);
    m_manager->updateResize(dragPos);

    const QRect resized = m_manager->selectionRect();
    QVERIFY2(bounds.contains(resized),
             qPrintable(QStringLiteral("Resize escaped bounds: %1,%2 %3x%4")
                            .arg(resized.x()).arg(resized.y())
                            .arg(resized.width()).arg(resized.height())));
    QVERIFY(resized.width() >= 10);
    QVERIFY(resized.height() >= 10);
    QVERIFY(resized.size() != originalRect.size());
    QVERIFY(qAbs(static_cast<qreal>(resized.width()) / resized.height() - ratio) < 0.02);

    switch (handle) {
    case Handle::Top:
        QCOMPARE(resized.bottom(), originalRect.bottom());
        QVERIFY(qAbs((resized.left() + resized.right()) -
                     (originalRect.left() + originalRect.right())) <= 1);
        break;
    case Handle::Bottom:
        QCOMPARE(resized.top(), originalRect.top());
        QVERIFY(qAbs((resized.left() + resized.right()) -
                     (originalRect.left() + originalRect.right())) <= 1);
        break;
    case Handle::Left:
        QCOMPARE(resized.right(), originalRect.right());
        QVERIFY(qAbs((resized.top() + resized.bottom()) -
                     (originalRect.top() + originalRect.bottom())) <= 1);
        break;
    case Handle::Right:
        QCOMPARE(resized.left(), originalRect.left());
        QVERIFY(qAbs((resized.top() + resized.bottom()) -
                     (originalRect.top() + originalRect.bottom())) <= 1);
        break;
    default:
        QFAIL("Expected an edge resize handle");
    }
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
