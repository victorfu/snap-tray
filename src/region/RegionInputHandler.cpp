#include "region/RegionInputHandler.h"
#include "region/SelectionStateManager.h"
#include "region/SelectionResizeHelper.h"
#include "region/TextAnnotationEditor.h"
#include "region/MultiRegionManager.h"
#include "region/UpdateThrottler.h"
#include "annotation/AnnotationPerfTrace.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"
#include "InlineTextEditor.h"
#include "TransformationGizmo.h"
#include "tools/ToolTraits.h"
#include "platform/WindowLevel.h"

#include <QMouseEvent>
#include <QWidget>
#include <QDebug>
#include <QTextEdit>
#include <QCursor>
#include <QtGlobal>

namespace {
bool shouldSuppressCompletedSelectionDragUi(const RegionInputState& state,
                                            const SelectionStateManager* selectionManager)
{
    return selectionManager &&
           state.completedSelectionDragUiSuppressed &&
           selectionManager->hasSelection();
}
} // namespace

RegionInputHandler::RegionInputHandler(QObject* parent)
    : QObject(parent)
{
    m_dragFrameTimer.setTimerType(Qt::PreciseTimer);
    m_dragFrameTimer.setInterval(SelectionDirtyRegionPlanner::kDragFrameIntervalMs);
    connect(&m_dragFrameTimer, &QTimer::timeout, this, &RegionInputHandler::onDragFrameTick);
}

void RegionInputHandler::setSelectionManager(SelectionStateManager* manager)
{
    m_selectionManager = manager;
}

void RegionInputHandler::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

void RegionInputHandler::setToolManager(ToolManager* manager)
{
    m_toolManager = manager;
}

void RegionInputHandler::setTextEditor(InlineTextEditor* editor)
{
    m_textEditor = editor;
}

void RegionInputHandler::setTextAnnotationEditor(TextAnnotationEditor* editor)
{
    m_textAnnotationEditor = editor;
}

void RegionInputHandler::setMultiRegionManager(MultiRegionManager* manager)
{
    m_multiRegionManager = manager;
}

void RegionInputHandler::setUpdateThrottler(UpdateThrottler* throttler)
{
    m_updateThrottler = throttler;
}

void RegionInputHandler::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
}

void RegionInputHandler::setSharedState(RegionInputState* state)
{
    m_state = state;
}

RegionInputState& RegionInputHandler::state()
{
    Q_ASSERT(m_state != nullptr);
    return *m_state;
}

const RegionInputState& RegionInputHandler::state() const
{
    Q_ASSERT(m_state != nullptr);
    return *m_state;
}

void RegionInputHandler::resetDirtyTracking()
{
    m_lastMagnifierRect = QRect();
    m_lastCrosshairPoint = QPoint();
    m_lastSelectionRect = QRect();
    m_lastToolbarRect = QRect();
    m_pendingWindowClickActive = false;
    m_pendingWindowClickRect = QRect();
    m_pendingWindowClickStartPos = QPoint();
    m_dragFrameTimer.stop();
}

void RegionInputHandler::seedDirtyTrackingForCurrentState()
{
    if (!m_parentWidget || !m_selectionManager || !m_state) {
        return;
    }

    m_lastCrosshairPoint = state().currentPoint;
    m_lastSelectionRect = m_selectionManager->selectionRect().normalized();
    m_lastMagnifierRect = m_dirtyRegionPlanner.magnifierRectForCursor(
        state().currentPoint, m_parentWidget->size());
    m_lastToolbarRect = QRect();
}

// ============================================================================
// Main Event Handlers
// ============================================================================

void RegionInputHandler::handleMousePress(QMouseEvent* event)
{
    if (!m_state) {
        qWarning() << "RegionInputHandler: Shared input state not set";
        return;
    }

    m_currentModifiers = event->modifiers();

    if (event->button() == Qt::LeftButton) {
        if (m_selectionManager->isComplete()) {
            // Handle inline text editing
            if (handleTextEditorPress(event->pos())) {
                return;
            }

            // Finalize polyline when clicking on UI elements
            auto finalizePolylineForUiClick = [&](const QPoint& pos) {
                if (state().currentTool == ToolId::Arrow && m_toolManager->isDrawing()) {
                    m_toolManager->handleDoubleClick(pos);
                    state().isDrawing = m_toolManager->isDrawing();
                    emit drawingStateChanged(state().isDrawing);
                }
                };

            // Text interaction is centralized in TextToolHandler and stays active
            // globally (not only when Text is the current tool).
            if (m_toolManager &&
                m_toolManager->handleTextInteractionPress(event->pos(), event->modifiers())) {
                return;
            }

            if (m_toolManager &&
                m_toolManager->handleEmojiInteractionPress(event->pos(), event->modifiers())) {
                syncHoverCursorAt(event->pos());
                return;
            }

            if (m_toolManager &&
                m_toolManager->handleArrowInteractionPress(event->pos(), event->modifiers())) {
                syncHoverCursorAt(event->pos());
                return;
            }

            if (m_toolManager &&
                m_toolManager->handlePolylineInteractionPress(event->pos(), event->modifiers())) {
                syncHoverCursorAt(event->pos());
                return;
            }

            // Check shape annotations after arrow/polyline so top-most handles win.
            if (m_toolManager &&
                m_toolManager->handleShapeInteractionPress(event->pos(), event->modifiers())) {
                return;
            }

            // Clear selection if clicking elsewhere.
            if (m_annotationLayer->selectedIndex() >= 0) {
                bool clearedSelectedEmojiInEmojiTool = false;
                if (state().currentTool == ToolId::EmojiSticker) {
                    clearedSelectedEmojiInEmojiTool =
                        dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem()) != nullptr;
                }
                m_annotationLayer->clearSelection();
                emit updateRequested();
                if (clearedSelectedEmojiInEmojiTool) {
                    // Keep this click for deselection only; do not place a new emoji.
                    return;
                }
            }

            // Handle annotation tool inside selection
            if (handleAnnotationToolPress(event->pos())) {
                return;
            }

            // Handle Selection tool
            if (handleSelectionToolPress(event->pos())) {
                return;
            }
        }
        else {
            // Start new selection
            handleNewSelectionPress(event->pos());
        }
        emit updateRequested();
    }
    else if (event->button() == Qt::RightButton) {
        handleRightButtonPress(event->pos());
    }
}

void RegionInputHandler::handleMouseMove(QMouseEvent* event)
{
    if (!m_state) {
        qWarning() << "RegionInputHandler: Shared input state not set";
        return;
    }

    m_currentModifiers = event->modifiers();
    state().currentPoint = event->pos();

    // Race condition recovery: mouse button was already pressed when window appeared
    // Qt doesn't fire mousePressEvent for already-pressed buttons, so we detect
    // this broken state here and synthesize the missing selection start
    if (!m_selectionManager->hasActiveSelection() &&
        (event->buttons() & Qt::LeftButton)) {
        qDebug() << "RegionInputHandler: Recovering from race condition - "
                 << "starting selection from mouseMoveEvent";
        handleNewSelectionPress(event->pos());
    }

    // Keep crosshair sticky during pre-selection idle.
    // System hot zones (Dock/Taskbar) may temporarily force Arrow cursor;
    // by re-applying Tool/Cross on every move in this state we restore it.
    if (!m_selectionManager->hasActiveSelection() && !state().isDrawing && m_parentWidget) {
        auto& cm = CursorManager::instance();
        cm.setHoverTargetForWidget(m_parentWidget, HoverTarget::None);
        cm.pushCursorForWidget(m_parentWidget, CursorContext::Tool, Qt::CrossCursor);
    }

    // Handle text editor in confirm mode
    if (handleTextEditorMove(event->pos())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleEmojiInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleShapeInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleArrowInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handlePolylineInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    // Window detection during hover
    handleWindowDetectionMove(event->pos());

    // Handle selection states
    if (m_selectionManager->isSelecting()) {
        handleSelectionMove(event->pos());
    }
    else if (m_selectionManager->isResizing()) {
        m_selectionManager->updateResize(event->pos());
    }
    else if (m_selectionManager->isMoving()) {
        m_selectionManager->updateMove(event->pos());
    }
    else if (state().isDrawing) {
        handleAnnotationMove(event->pos());
    }
    else if (m_selectionManager->isComplete() ||
        (state().multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0)) {
        handleHoverMove(event->pos(), event->buttons());
    }

    // Throttled updates
    handleThrottledUpdate();
}

void RegionInputHandler::syncHoverCursorAt(const QPoint& pos)
{
    handleHoverMove(pos, Qt::NoButton);
}

void RegionInputHandler::handleMouseRelease(QMouseEvent* event)
{
    if (!m_state) {
        qWarning() << "RegionInputHandler: Shared input state not set";
        return;
    }

    m_currentModifiers = event->modifiers();

    if (event->button() == Qt::LeftButton) {
        // Handle text editor drag release
        if (handleTextEditorRelease(event->pos())) {
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleTextInteractionRelease(event->pos(), event->modifiers())) {
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleEmojiInteractionRelease(event->pos(), event->modifiers())) {
            syncHoverCursorAt(event->pos());
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleShapeInteractionRelease(event->pos(), event->modifiers())) {
            return;
        }

        if (m_toolManager &&
            m_toolManager->handleArrowInteractionRelease(event->pos(), event->modifiers())) {
            syncHoverCursorAt(event->pos());
            return;
        }

        if (m_toolManager &&
            m_toolManager->handlePolylineInteractionRelease(event->pos(), event->modifiers())) {
            syncHoverCursorAt(event->pos());
            return;
        }

        // Handle selection release
        if (m_selectionManager->isSelecting()) {
            handleSelectionRelease(event->pos());
            clearSelectionDrag();
            emit updateRequested();
        }
        else if (m_selectionManager->isResizing()) {
            m_selectionManager->finishResize();
            clearSelectionDrag();
            emit updateRequested();
        }
        else if (m_selectionManager->isMoving()) {
            m_selectionManager->finishMove();
            clearSelectionDrag();
            emit updateRequested();
        }
        else if (state().isDrawing) {
            handleAnnotationRelease();
        }
    }
}

// ============================================================================
// Mouse Press Helpers
// ============================================================================

bool RegionInputHandler::handleTextEditorPress(const QPoint& pos)
{
    if (!m_textEditor) {
        return false;
    }

    if (!m_textEditor->isEditing()) {
        return false;
    }

    if (m_textEditor->isConfirmMode()) {
        if (!m_textEditor->contains(pos)) {
            m_textEditor->finishEditing();
            return false;
        }
        m_textEditor->handleMousePress(pos);
        return true;
    }

    // In typing mode
    if (!m_textEditor->contains(pos)) {
        if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
            m_textEditor->finishEditing();
        }
        else {
            m_textEditor->cancelEditing();
        }
        return false;
    }

    return true;
}

bool RegionInputHandler::handleAnnotationToolPress(const QPoint& pos)
{
    QRect sel = m_selectionManager->selectionRect();
    const bool requiresSelectionContainment = (state().currentTool != ToolId::Text);
    const bool insideSelection = sel.contains(pos);
    if (isAnnotationTool(state().currentTool) &&
        state().currentTool != ToolId::Selection &&
        (!requiresSelectionContainment || insideSelection)) {
        startAnnotation(pos);
        return true;
    }
    return false;
}

bool RegionInputHandler::handleSelectionToolPress(const QPoint& pos)
{
    if (state().currentTool != ToolId::Selection) {
        return false;
    }

    if (state().multiRegionMode && m_multiRegionManager) {
        if (state().replaceTargetIndex >= 0) {
            handleNewSelectionPress(pos);
            emit updateRequested();
            return true;
        }

        int hitIndex = m_multiRegionManager->hitTest(pos);
        if (hitIndex >= 0) {
            m_multiRegionManager->setActiveIndex(hitIndex);
            m_selectionManager->setSelectionRect(m_multiRegionManager->regionRect(hitIndex));

            auto handle = m_selectionManager->hitTestHandle(pos);
            if (handle != SelectionStateManager::ResizeHandle::None) {
                m_lastSelectionRect = m_selectionManager->selectionRect();
                clearSelectionDrag();
                m_selectionManager->startResize(pos, handle);
                emit updateRequested();
                return true;
            }

            if (m_selectionManager->hitTestMove(pos)) {
                m_lastSelectionRect = m_selectionManager->selectionRect();
                m_selectionManager->startMove(pos);
                beginSelectionDrag();
                emit updateRequested();
                return true;
            }

            return true;
        }

        // Check if clicking on active region's resize handles (handles extend outside rect)
        int activeIndex = m_multiRegionManager->activeIndex();
        if (activeIndex >= 0) {
            QRect activeRect = m_multiRegionManager->regionRect(activeIndex);
            m_selectionManager->setSelectionRect(activeRect);
            auto handle = m_selectionManager->hitTestHandle(pos);
            if (handle != SelectionStateManager::ResizeHandle::None) {
                m_lastSelectionRect = m_selectionManager->selectionRect();
                clearSelectionDrag();
                m_selectionManager->startResize(pos, handle);
                emit updateRequested();
                return true;
            }
        }

        // In multi-region mode, clicking outside any region starts a new selection
        m_multiRegionManager->setActiveIndex(-1);
        handleNewSelectionPress(pos);
        emit updateRequested();
        return true;
    }

    auto handle = m_selectionManager->hitTestHandle(pos);
    if (handle != SelectionStateManager::ResizeHandle::None) {
        m_lastSelectionRect = m_selectionManager->selectionRect();
        clearSelectionDrag();
        m_selectionManager->startResize(pos, handle);
        emit updateRequested();
        return true;
    }

    if (m_selectionManager->hitTestMove(pos)) {
        m_lastSelectionRect = m_selectionManager->selectionRect();
        m_selectionManager->startMove(pos);
        beginSelectionDrag();
        emit updateRequested();
        return true;
    }

    // Click outside - adjust edges
    QRect sel = m_selectionManager->selectionRect();
    SelectionStateManager::ResizeHandle outsideHandle = determineHandleFromOutsideClick(pos, sel);
    if (outsideHandle != SelectionStateManager::ResizeHandle::None) {
        adjustEdgesToPosition(pos, outsideHandle);
        emit updateRequested();
    }
    return true;
}

void RegionInputHandler::handleNewSelectionPress(const QPoint& pos)
{
    clearSelectionDrag();
    m_selectionManager->startSelection(pos);
    m_startPoint = pos;
    state().currentPoint = pos;
    m_lastSelectionRect = QRect();

    // Preserve the highlighted window at press time so minor pointer drift
    // still resolves as window-click instead of falling back to full screen.
    if (state().hasDetectedWindow && state().highlightedWindowRect.isValid()) {
        m_pendingWindowClickActive = true;
        m_pendingWindowClickRect = state().highlightedWindowRect;
        m_pendingWindowClickStartPos = pos;
    }
    else {
        m_pendingWindowClickActive = false;
        m_pendingWindowClickRect = QRect();
        m_pendingWindowClickStartPos = QPoint();
    }
}

void RegionInputHandler::handleRightButtonPress(const QPoint& pos)
{
    if (state().multiRegionMode && m_multiRegionManager) {
        clearSelectionDrag();
        int hitIndex = m_multiRegionManager->hitTest(pos);
        if (hitIndex >= 0) {
            m_multiRegionManager->removeRegion(hitIndex);
            int activeIndex = m_multiRegionManager->activeIndex();
            if (activeIndex >= 0) {
                m_selectionManager->setSelectionRect(m_multiRegionManager->regionRect(activeIndex));
            }
            else {
                m_selectionManager->clearSelection();
            }
            emit updateRequested();
            return;
        }
        return;
    }

    // Cancel capture if still selecting or not yet started
    if (m_selectionManager->isSelecting() || !m_selectionManager->isComplete()) {
        clearSelectionDrag();
        m_selectionManager->clearSelection();
        m_pendingWindowClickActive = false;
        m_pendingWindowClickRect = QRect();
        m_pendingWindowClickStartPos = QPoint();
        emit selectionCancelledByRightClick();
        return;
    }

    if (m_selectionManager->isComplete()) {
        m_pendingWindowClickActive = false;
        m_pendingWindowClickRect = QRect();
        m_pendingWindowClickStartPos = QPoint();
        if (state().isDrawing) {
            state().isDrawing = false;
            emit drawingStateChanged(false);
            m_toolManager->cancelDrawing();
            emit updateRequested();
            return;
        }
        if (m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
            m_selectionManager->cancelResizeOrMove();
            clearSelectionDrag();
            emit updateRequested();
            return;
        }
        clearSelectionDrag();
        m_selectionManager->clearSelection();
        m_annotationLayer->clear();
        CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Selecting);
        emit updateRequested();
    }
}

// ============================================================================
// Mouse Move Helpers
// ============================================================================

bool RegionInputHandler::handleTextEditorMove(const QPoint& pos)
{
    if (!m_textEditor) {
        return false;
    }

    if (m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseMove(pos);
        if (m_textEditor->contains(pos)) {
            CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Moving);
        }
        else {
            CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Idle);
        }
        emit updateRequested();
        return true;
    }
    return false;
}


void RegionInputHandler::handleWindowDetectionMove(const QPoint& pos)
{
    if (!m_selectionManager->hasActiveSelection()) {
        int dx = pos.x() - m_lastWindowDetectionPos.x();
        int dy = pos.y() - m_lastWindowDetectionPos.y();
        if (dx * dx + dy * dy >= WINDOW_DETECTION_MIN_DISTANCE_SQ) {
            emit windowDetectionRequested(pos);
            m_lastWindowDetectionPos = pos;
        }
    }
}

void RegionInputHandler::clearDetectionAndNotify()
{
    if (!m_state) {
        return;
    }

    if (!state().hasDetectedWindow && state().highlightedWindowRect.isNull()) {
        return;
    }

    const QRect previousHighlightRect = state().highlightedWindowRect;
    state().highlightedWindowRect = QRect();
    state().hasDetectedWindow = false;
    emit detectionCleared(previousHighlightRect);
}

void RegionInputHandler::handleSelectionMove(const QPoint& pos)
{
    if (m_pendingWindowClickActive) {
        const int dx = pos.x() - m_pendingWindowClickStartPos.x();
        const int dy = pos.y() - m_pendingWindowClickStartPos.y();
        if ((dx * dx + dy * dy) > WINDOW_CLICK_MAX_DISTANCE_SQ) {
            m_pendingWindowClickActive = false;
            m_pendingWindowClickRect = QRect();
            m_pendingWindowClickStartPos = QPoint();
        }
    }

    clearDetectionAndNotify();
    m_selectionManager->updateSelection(pos);
}

void RegionInputHandler::handleAnnotationMove(const QPoint& pos)
{
    updateAnnotation(pos);

    if (state().currentTool == ToolId::Mosaic) {
        emit toolCursorRequested();
    }
}

void RegionInputHandler::handleHoverMove(const QPoint& pos, Qt::MouseButtons buttons)
{
    auto& cm = CursorManager::instance();

    // Multi-region mode cursor handling - MUST be checked FIRST and return early
    if (state().multiRegionMode && m_multiRegionManager && m_parentWidget) {
        int activeIndex = m_multiRegionManager->activeIndex();

        // Active region: only show resize cursors on 8 handles
        if (activeIndex >= 0) {
            QRect activeRect = m_multiRegionManager->regionRect(activeIndex);
            m_selectionManager->setSelectionRect(activeRect);

            // Check handles - only these show resize cursors
            auto handle = SelectionResizeHelper::hitTestHandle(activeRect, pos);
            if (handle != SelectionStateManager::ResizeHandle::None) {
                cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::ResizeHandle, static_cast<int>(handle));
                return;
            }
        }

        // Everything else in multi-region mode: reset hover and let input state control
        cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::None);
        cm.setInputStateForWidget(m_parentWidget,InputState::Selecting);
        return;
    }

    // === Single-region mode handling below ===
    // Check gizmo handles first (highest priority for selected items)
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle != GizmoHandle::None) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    // Check shape gizmo handles first, then fall back to the shared
    // annotation hover path used by PinWindow and ScreenCanvas.
    if (auto* shapeItem = getSelectedShapeAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(shapeItem, pos);
        if (handle != GizmoHandle::None) {
            cm.setHoverTargetForWidget(m_parentWidget, HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    const auto annotationHit = CursorManager::hitTestAnnotations(
        pos,
        m_annotationLayer,
        getSelectedEmojiStickerAnnotation(),
        getSelectedArrowAnnotation(),
        getSelectedPolylineAnnotation(),
        state().currentTool != ToolId::EmojiSticker);
    if (annotationHit.hit) {
        cm.setHoverTargetForWidget(
            m_parentWidget, annotationHit.target, annotationHit.handleIndex);
        return;
    }

    if (m_annotationLayer->hitTestShape(pos) >= 0) {
        cm.setHoverTargetForWidget(m_parentWidget, HoverTarget::Annotation);
        return;
    }

    // Check resize handles for selection tool
    if (state().currentTool == ToolId::Selection) {
        auto handle = m_selectionManager->hitTestHandle(pos);
        if (handle != SelectionStateManager::ResizeHandle::None) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::ResizeHandle, static_cast<int>(handle));
            return;
        }

        // Check if hovering inside selection for move (displays SizeAllCursor)
        if (m_selectionManager->hitTestMove(pos)) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::SelectionBody);
            return;
        }
    }

    // No special hover target - reset and let tool cursor show
    cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::None);
    emit toolCursorRequested();

    // Handle eraser tool mouse move for hover highlighting
    if (state().currentTool == ToolId::Eraser && m_toolManager) {
        m_toolManager->handleMouseMove(pos, m_currentModifiers);
    }
}

void RegionInputHandler::handleThrottledUpdate()
{
    if (!m_updateThrottler || !m_parentWidget || !m_selectionManager) {
        return;
    }

    updateDragFramePump();

    // First frame after initialization: the initial show() painted magnifier + crosshair
    // at the initial cursor position, but m_lastMagnifierRect is still null (unset).
    // Do a full widget repaint to clear the initial content, then initialize tracking state.
    if (m_lastMagnifierRect.isNull()) {
        m_lastCrosshairPoint = state().currentPoint;
        m_lastSelectionRect = m_selectionManager->selectionRect().normalized();
        m_lastMagnifierRect = m_dirtyRegionPlanner.magnifierRectForCursor(
            state().currentPoint, m_parentWidget->size());
        const bool hasRenderableSelection =
            m_selectionManager->hasSelection() && m_lastSelectionRect.isValid();
        // QML toolbar and region control panel position themselves in separate windows
        m_lastToolbarRect = QRect();

        if (AnnotationPerfTrace::isEnabled("SNAPTRAY_REGION_PERF_TRACE")) {
            qDebug().noquote()
                << QStringLiteral("RegionEntryTrace")
                << QStringLiteral("stage=firstMoveFallbackRepaint")
                << QStringLiteral("point=%1,%2")
                       .arg(state().currentPoint.x())
                       .arg(state().currentPoint.y())
                << QStringLiteral("hasSelection=%1").arg(hasRenderableSelection ? 1 : 0);
        }

        m_parentWidget->update();  // Full repaint for first frame
        return;
    }

    if (m_selectionManager->isSelecting() || m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
        const QRect currentSelectionRect = m_selectionManager->selectionRect().normalized();
        const bool suppressFloatingUi =
            shouldSuppressCompletedSelectionDragUi(state(), m_selectionManager);
        const QRect currentMagnifierRect = suppressFloatingUi
            ? QRect()
            : m_dirtyRegionPlanner.magnifierRectForCursor(state().currentPoint, m_parentWidget->size());
        QRect currentToolbarRect;
        QRect currentRegionControlRect;

        SelectionDirtyRegionPlanner::SelectionDragParams params;
        params.currentSelectionRect = currentSelectionRect;
        params.lastSelectionRect = m_lastSelectionRect;
        params.currentToolbarRect = currentToolbarRect;
        params.lastToolbarRect = m_lastToolbarRect;
        params.currentRegionControlRect = currentRegionControlRect;
        params.lastRegionControlRect = QRect();
        params.currentMagnifierRect = currentMagnifierRect;
        params.lastMagnifierRect = m_lastMagnifierRect;
        params.currentCursorPos = state().currentPoint;
        params.lastCursorPos = m_lastCrosshairPoint;
        params.viewportSize = m_parentWidget->size();
        params.suppressFloatingUi = suppressFloatingUi;
        params.includeMagnifier = !suppressFloatingUi;
        const QRegion dirtyRegion = m_dirtyRegionPlanner.planSelectionDragRegion(params);
        m_parentWidget->update(dirtyRegion);
        m_lastSelectionRect = currentSelectionRect;
        m_lastMagnifierRect = currentMagnifierRect;
        m_lastToolbarRect = currentToolbarRect;
        m_lastCrosshairPoint = state().currentPoint;
    }
    else if (state().isDrawing) {
        return;
    }
    else if (m_selectionManager->isComplete()) {
        if (m_updateThrottler->shouldUpdate(UpdateThrottler::ThrottleType::Hover)) {
            m_updateThrottler->reset(UpdateThrottler::ThrottleType::Hover);
            emit updateRequested();
        }
    }
    else {
        // No throttle — update on every mouse move for instant magnifier feedback
        const QRect currentMagnifierRect = m_dirtyRegionPlanner.magnifierRectForCursor(
            state().currentPoint, m_parentWidget->size());

        SelectionDirtyRegionPlanner::HoverParams params;
        params.currentMagnifierRect = currentMagnifierRect;
        params.lastMagnifierRect = m_lastMagnifierRect;
        params.currentCursorPos = state().currentPoint;
        params.lastCursorPos = m_lastCrosshairPoint;
        params.viewportSize = m_parentWidget->size();
        const QRegion dirtyRegion = m_dirtyRegionPlanner.planHoverRegion(params);
        m_parentWidget->update(dirtyRegion);
        m_lastMagnifierRect = currentMagnifierRect;
        m_lastCrosshairPoint = state().currentPoint;
    }
}

void RegionInputHandler::updateDragFramePump()
{
    if (!m_selectionManager || !m_parentWidget) {
        m_dragFrameTimer.stop();
        return;
    }

    const bool shouldRun = m_selectionManager->isSelecting() ||
                           m_selectionManager->isResizing() ||
                           m_selectionManager->isMoving();
    if (shouldRun) {
        if (!m_dragFrameTimer.isActive()) {
            m_dragFrameTimer.start();
        }
        return;
    }

    if (m_dragFrameTimer.isActive()) {
        m_dragFrameTimer.stop();
    }
}

void RegionInputHandler::onDragFrameTick()
{
    if (!m_parentWidget || !m_selectionManager || !m_state) {
        m_dragFrameTimer.stop();
        return;
    }

    if (!m_selectionManager->isSelecting() &&
        !m_selectionManager->isResizing() &&
        !m_selectionManager->isMoving()) {
        m_dragFrameTimer.stop();
        return;
    }

    const QPoint localPos = currentCursorLocalPos();
    if (localPos == state().currentPoint) {
        return;
    }

    state().currentPoint = localPos;
    emit currentPointUpdated(localPos);
    if (m_selectionManager->isSelecting()) {
        handleSelectionMove(localPos);
    }
    else if (m_selectionManager->isResizing()) {
        m_selectionManager->updateResize(localPos);
    }
    else if (m_selectionManager->isMoving()) {
        m_selectionManager->updateMove(localPos);
    }

    handleThrottledUpdate();
}

QPoint RegionInputHandler::currentCursorLocalPos() const
{
    if (!m_parentWidget) {
        return QPoint();
    }

    QPoint localPos = m_parentWidget->mapFromGlobal(QCursor::pos());
    const int maxX = qMax(0, m_parentWidget->width() - 1);
    const int maxY = qMax(0, m_parentWidget->height() - 1);
    localPos.setX(qBound(0, localPos.x(), maxX));
    localPos.setY(qBound(0, localPos.y(), maxY));
    return localPos;
}

// ============================================================================
// Mouse Release Helpers
// ============================================================================

bool RegionInputHandler::handleTextEditorRelease(const QPoint& pos)
{
    if (!m_textEditor) {
        return false;
    }

    if (m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseRelease(pos);
        return true;
    }
    return false;
}

void RegionInputHandler::handleSelectionRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    auto clearPendingWindowClick = [this]() {
        m_pendingWindowClickActive = false;
        m_pendingWindowClickRect = QRect();
        m_pendingWindowClickStartPos = QPoint();
    };

    QRect sel = m_selectionManager->selectionRect();
    const bool canUsePendingWindowClick =
        m_pendingWindowClickActive && m_pendingWindowClickRect.isValid();

    if (state().multiRegionMode) {
        const bool replacingRegion = (state().replaceTargetIndex >= 0);
        if (sel.width() > 5 && sel.height() > 5) {
            m_selectionManager->finishSelection();
            emit selectionFinished();
        }
        else if (state().hasDetectedWindow && state().highlightedWindowRect.isValid()) {
            m_selectionManager->setFromDetectedWindow(state().highlightedWindowRect);
            emit selectionFinished();

            clearDetectionAndNotify();
        }
        else if (canUsePendingWindowClick) {
            m_selectionManager->setFromDetectedWindow(m_pendingWindowClickRect);
            emit selectionFinished();

            clearDetectionAndNotify();
        }
        else if (replacingRegion) {
            m_selectionManager->clearSelection();
            emit replaceSelectionCancelled();
        }
        clearPendingWindowClick();
        return;
    }

    if (sel.width() > 5 && sel.height() > 5) {
        m_selectionManager->finishSelection();
        emit selectionFinished();
    }
    else if (state().hasDetectedWindow && state().highlightedWindowRect.isValid()) {
        m_selectionManager->setFromDetectedWindow(state().highlightedWindowRect);
        emit selectionFinished();

        clearDetectionAndNotify();
    }
    else if (canUsePendingWindowClick) {
        m_selectionManager->setFromDetectedWindow(m_pendingWindowClickRect);
        emit selectionFinished();

        clearDetectionAndNotify();
    }
    else {
        emit fullScreenSelectionRequested();
        emit selectionFinished();
    }
    clearPendingWindowClick();
}

void RegionInputHandler::handleAnnotationRelease()
{
    finishAnnotation();
}

// ============================================================================
// Annotation Helpers
// ============================================================================

void RegionInputHandler::startAnnotation(const QPoint& pos)
{
    if (ToolTraits::isToolManagerHandledTool(state().currentTool)) {
        m_toolManager->setColor(state().annotationColor);
        // Don't overwrite width for StepBadge - it uses a separate radius setting
        if (state().currentTool != ToolId::StepBadge) {
            m_toolManager->setWidth(state().annotationWidth);
        }
        m_toolManager->setArrowStyle(static_cast<LineEndStyle>(state().arrowStyle));
        m_toolManager->setLineStyle(static_cast<LineStyle>(state().lineStyle));
        m_toolManager->setShapeType(static_cast<int>(state().shapeType));
        m_toolManager->setShapeFillMode(static_cast<int>(state().shapeFillMode));

        m_toolManager->setCurrentTool(state().currentTool);
        m_toolManager->handleMousePress(pos, m_currentModifiers);
        state().isDrawing = m_toolManager->isDrawing();

        if (!state().isDrawing && (state().currentTool == ToolId::StepBadge ||
            state().currentTool == ToolId::EmojiSticker)) {
            state().isDrawing = true;
        }
        emit drawingStateChanged(state().isDrawing);
        return;
    }
}

void RegionInputHandler::updateAnnotation(const QPoint& pos)
{
    if (ToolTraits::isToolManagerHandledTool(state().currentTool)) {
        m_toolManager->handleMouseMove(pos, m_currentModifiers);
    }
}

void RegionInputHandler::finishAnnotation()
{
    if (ToolTraits::isToolManagerHandledTool(state().currentTool)) {
        m_toolManager->handleMouseRelease(state().currentPoint, m_currentModifiers);
        state().isDrawing = m_toolManager->isDrawing();
    }
    else {
        state().isDrawing = false;
    }
    emit drawingStateChanged(state().isDrawing);
}

void RegionInputHandler::beginSelectionDrag()
{
    if (!m_parentWidget) {
        return;
    }

    CursorManager::instance().setDragStateForWidget(
        m_parentWidget, DragState::SelectionDrag);
}

void RegionInputHandler::clearSelectionDrag()
{
    if (!m_parentWidget) {
        return;
    }

    CursorManager::instance().setDragStateForWidget(
        m_parentWidget, DragState::None);
}

// ============================================================================
// Selection Helpers
// ============================================================================

SelectionStateManager::ResizeHandle RegionInputHandler::determineHandleFromOutsideClick(
    const QPoint& pos, const QRect& sel) const
{
    return SelectionResizeHelper::determineHandleFromOutsideClick(pos, sel);
}

void RegionInputHandler::adjustEdgesToPosition(const QPoint& pos,
    SelectionStateManager::ResizeHandle handle)
{
    QRect currentRect = m_selectionManager->selectionRect();
    QRect newRect = SelectionResizeHelper::adjustEdgesToPosition(pos, handle, currentRect);

    if (SelectionResizeHelper::meetsMinimumSize(newRect)) {
        m_selectionManager->setSelectionRect(newRect);
    }
}

// ============================================================================
// Utility Methods
// ============================================================================

TextBoxAnnotation* RegionInputHandler::getSelectedTextAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

EmojiStickerAnnotation* RegionInputHandler::getSelectedEmojiStickerAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<EmojiStickerAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

ShapeAnnotation* RegionInputHandler::getSelectedShapeAnnotation() const
{
    if (!m_annotationLayer) {
        return nullptr;
    }
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<ShapeAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

bool RegionInputHandler::isAnnotationTool(ToolId tool) const
{
    return ToolTraits::isAnnotationTool(tool);
}

ArrowAnnotation* RegionInputHandler::getSelectedArrowAnnotation() const
{
    if (!m_annotationLayer) return nullptr;
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<ArrowAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

PolylineAnnotation* RegionInputHandler::getSelectedPolylineAnnotation() const {
    if (!m_annotationLayer) return nullptr;
    if (m_annotationLayer->selectedIndex() < 0) return nullptr;
    return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->selectedItem());
}
