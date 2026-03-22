#include <QtTest>

#include <QMouseEvent>
#include <QSignalSpy>
#include <QWidget>

#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorManager.h"
#include "region/RegionInputHandler.h"
#include "region/RegionInputState.h"
#include "region/SelectionStateManager.h"
#include "tools/ToolManager.h"

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
    void testSelectionMoveSetsAndClearsDragStateOnRelease();
    void testSelectionMoveClearsDragStateOnRightClickCancel();
    void testDrawingMoveDoesNotEmitLegacyFullUpdate();
    void testArrowInteractionDoesNotEmitLegacyFullUpdate();
    void testEmojiInteractionDoesNotEmitLegacyFullUpdate();
    void testPolylineInteractionDoesNotEmitLegacyFullUpdate();
    void testSeedDirtyTrackingInitializesHoverState();
    void testEmojiInteractionPressAndReleaseRefreshHoverCursor();
    void testPolylineInteractionPressAndReleaseRefreshHoverCursor();

private:
    RegionInputHandler* m_handler = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    QWidget* m_parentWidget = nullptr;
    ToolManager* m_toolManager = nullptr;
    RegionInputState m_state;
};

void tst_RegionInputHandler::init()
{
    m_handler = new RegionInputHandler();
    m_selectionManager = new SelectionStateManager();
    m_annotationLayer = new AnnotationLayer();
    m_parentWidget = new QWidget();
    m_toolManager = new ToolManager();
    m_selectionManager->setBounds(QRect(0, 0, 1920, 1080));
    m_parentWidget->resize(1920, 1080);
    CursorManager::instance().registerWidget(m_parentWidget);

    m_toolManager->registerDefaultHandlers();
    m_toolManager->setAnnotationLayer(m_annotationLayer);

    m_state = RegionInputState();
    m_handler->setSelectionManager(m_selectionManager);
    m_handler->setAnnotationLayer(m_annotationLayer);
    m_handler->setToolManager(m_toolManager);
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

    delete m_toolManager;
    m_toolManager = nullptr;

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

void tst_RegionInputHandler::testDrawingMoveDoesNotEmitLegacyFullUpdate()
{
    m_state.currentTool = ToolId::Pencil;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);
    QVERIFY(m_state.isDrawing);

    QSignalSpy fullUpdateSpy(m_handler, qOverload<>(&RegionInputHandler::updateRequested));
    QSignalSpy rectUpdateSpy(m_handler, qOverload<const QRect&>(&RegionInputHandler::updateRequested));

    auto moveEvent = makeMouseEvent(
        QEvent::MouseMove, QPoint(190, 205), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    QCOMPARE(fullUpdateSpy.count(), 0);
    QCOMPARE(rectUpdateSpy.count(), 0);
}

void tst_RegionInputHandler::testArrowInteractionDoesNotEmitLegacyFullUpdate()
{
    m_state.currentTool = ToolId::Arrow;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto arrow = std::make_unique<ArrowAnnotation>(
        QPoint(150, 170), QPoint(260, 230), Qt::red, 3);
    m_annotationLayer->addItem(std::move(arrow));
    m_annotationLayer->setSelectedIndex(0);

    QSignalSpy fullUpdateSpy(m_handler, qOverload<>(&RegionInputHandler::updateRequested));
    QSignalSpy rectUpdateSpy(m_handler, qOverload<const QRect&>(&RegionInputHandler::updateRequested));

    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, QPoint(200, 200), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto releaseEvent = makeMouseEvent(
        QEvent::MouseButtonRelease, QPoint(200, 200), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullUpdateSpy.count(), 0);
    QCOMPARE(rectUpdateSpy.count(), 0);
}

void tst_RegionInputHandler::testEmojiInteractionDoesNotEmitLegacyFullUpdate()
{
    m_state.currentTool = ToolId::EmojiSticker;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto emoji = std::make_unique<EmojiStickerAnnotation>(QPoint(220, 210), QStringLiteral("🙂"));
    m_annotationLayer->addItem(std::move(emoji));
    m_annotationLayer->setSelectedIndex(0);

    QSignalSpy fullUpdateSpy(m_handler, qOverload<>(&RegionInputHandler::updateRequested));
    QSignalSpy rectUpdateSpy(m_handler, qOverload<const QRect&>(&RegionInputHandler::updateRequested));

    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, QPoint(220, 210), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto releaseEvent = makeMouseEvent(
        QEvent::MouseButtonRelease, QPoint(220, 210), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullUpdateSpy.count(), 0);
    QCOMPARE(rectUpdateSpy.count(), 0);
}

void tst_RegionInputHandler::testPolylineInteractionDoesNotEmitLegacyFullUpdate()
{
    m_state.currentTool = ToolId::Polyline;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto polyline = std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(160, 170), QPoint(220, 210), QPoint(280, 230)},
        Qt::red,
        3);
    m_annotationLayer->addItem(std::move(polyline));
    m_annotationLayer->setSelectedIndex(0);

    QSignalSpy fullUpdateSpy(m_handler, qOverload<>(&RegionInputHandler::updateRequested));
    QSignalSpy rectUpdateSpy(m_handler, qOverload<const QRect&>(&RegionInputHandler::updateRequested));

    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, QPoint(220, 210), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto releaseEvent = makeMouseEvent(
        QEvent::MouseButtonRelease, QPoint(220, 210), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullUpdateSpy.count(), 0);
    QCOMPARE(rectUpdateSpy.count(), 0);
}

void tst_RegionInputHandler::testSeedDirtyTrackingInitializesHoverState()
{
    m_state.currentPoint = QPoint(320, 240);
    m_selectionManager->setSelectionRect(QRect(QPoint(420, 360), QPoint(180, 150)));

    m_handler->seedDirtyTrackingForCurrentState();

    QCOMPARE(m_handler->m_lastCrosshairPoint, m_state.currentPoint);
    QCOMPARE(m_handler->m_lastSelectionRect, m_selectionManager->selectionRect().normalized());
    QCOMPARE(
        m_handler->m_lastMagnifierRect,
        m_handler->m_dirtyRegionPlanner.magnifierRectForCursor(m_state.currentPoint, m_parentWidget->size()));
    QVERIFY(m_handler->m_lastToolbarRect.isNull());
}

void tst_RegionInputHandler::testEmojiInteractionPressAndReleaseRefreshHoverCursor()
{
    m_state.currentTool = ToolId::Selection;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto emoji = std::make_unique<EmojiStickerAnnotation>(QPoint(220, 210), QStringLiteral("🙂"));
    auto* emojiPtr = emoji.get();
    m_annotationLayer->addItem(std::move(emoji));
    m_annotationLayer->setSelectedIndex(0);

    const QPoint interactionPos = emojiPtr->center().toPoint();

    CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::ToolbarButton);
    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, interactionPos, Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    QCOMPARE(CursorManager::instance().hoverTargetForWidget(m_parentWidget), HoverTarget::GizmoHandle);

    CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::ToolbarButton);
    auto releaseEvent = makeMouseEvent(
        QEvent::MouseButtonRelease, interactionPos, Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(CursorManager::instance().hoverTargetForWidget(m_parentWidget), HoverTarget::GizmoHandle);
}

void tst_RegionInputHandler::testPolylineInteractionPressAndReleaseRefreshHoverCursor()
{
    m_state.currentTool = ToolId::Selection;
    m_selectionManager->setSelectionRect(QRect(100, 120, 300, 200));
    QVERIFY(m_selectionManager->isComplete());

    auto polyline = std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(160, 170), QPoint(220, 210), QPoint(280, 230)},
        Qt::red,
        3);
    m_annotationLayer->addItem(std::move(polyline));
    m_annotationLayer->setSelectedIndex(0);

    const QPoint interactionPos(220, 210);

    CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::ToolbarButton);
    auto pressEvent = makeMouseEvent(
        QEvent::MouseButtonPress, interactionPos, Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    QCOMPARE(CursorManager::instance().hoverTargetForWidget(m_parentWidget), HoverTarget::GizmoHandle);

    CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::ToolbarButton);
    auto releaseEvent = makeMouseEvent(
        QEvent::MouseButtonRelease, interactionPos, Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(CursorManager::instance().hoverTargetForWidget(m_parentWidget), HoverTarget::GizmoHandle);
}

QTEST_MAIN(tst_RegionInputHandler)
#include "tst_RegionInputHandler.moc"
