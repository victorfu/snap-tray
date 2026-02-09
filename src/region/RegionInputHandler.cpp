#include "region/RegionInputHandler.h"
#include "region/SelectionStateManager.h"
#include "region/SelectionResizeHelper.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionControlWidget.h"
#include "region/MultiRegionManager.h"
#include "region/UpdateThrottler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"
#include "toolbar/ToolbarCore.h"
#include "InlineTextEditor.h"
#include "toolbar/ToolOptionsPanel.h"
#include "EmojiPicker.h"
#include "TransformationGizmo.h"
#include "tools/ToolTraits.h"
#include "platform/WindowLevel.h"

#include <QMouseEvent>
#include <QWidget>
#include <QDebug>
#include <QTextEdit>
#include <QCursor>
#include <QtGlobal>

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

void RegionInputHandler::setToolbar(ToolbarCore* toolbar)
{
    m_toolbar = toolbar;
}

void RegionInputHandler::setTextEditor(InlineTextEditor* editor)
{
    m_textEditor = editor;
}

void RegionInputHandler::setTextAnnotationEditor(TextAnnotationEditor* editor)
{
    m_textAnnotationEditor = editor;
}

void RegionInputHandler::setColorAndWidthWidget(ToolOptionsPanel* widget)
{
    m_colorAndWidthWidget = widget;
}

void RegionInputHandler::setEmojiPicker(EmojiPicker* picker)
{
    m_emojiPicker = picker;
}

void RegionInputHandler::setRegionControlWidget(RegionControlWidget* widget)
{
    m_regionControlWidget = widget;
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
    m_lastRegionControlRect = QRect();
    m_dragFrameTimer.stop();
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

            // Check toolbar
            if (handleToolbarPress(event->pos())) {
                finalizePolylineForUiClick(event->pos());
                return;
            }

            // Check region control widget (radius + aspect ratio)
            if (handleRegionControlWidgetPress(event->pos())) {
                return;
            }

            // Check color/width widgets
            if (handleColorWidgetPress(event->pos())) {
                finalizePolylineForUiClick(event->pos());
                return;
            }

            // Text interaction is centralized in TextToolHandler and stays active
            // globally (not only when Text is the current tool).
            if (m_toolManager &&
                m_toolManager->handleTextInteractionPress(event->pos(), event->modifiers())) {
                return;
            }

            // Check emoji sticker annotations
            if (handleEmojiStickerAnnotationPress(event->pos())) {
                return;
            }

            // Check arrow annotations
            if (handleArrowAnnotationPress(event->pos())) {
                return;
            }

            // Check polyline annotations
            if (handlePolylineAnnotationPress(event->pos())) {
                return;
            }

            // Clear selection if clicking elsewhere
            if (m_annotationLayer->selectedIndex() >= 0) {
                m_annotationLayer->clearSelection();
                emit updateRequested();
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
#ifdef Q_OS_MAC
        forceNativeCrosshairCursor(m_parentWidget);
#endif
    }

    // Handle text editor in confirm mode
    if (handleTextEditorMove(event->pos())) {
        return;
    }

    if (m_toolManager &&
        m_toolManager->handleTextInteractionMove(event->pos(), event->modifiers())) {
        return;
    }

    // Handle emoji sticker dragging/scaling
    if (handleEmojiStickerMove(event->pos())) {
        return;
    }

    // Handle arrow annotation dragging/handle manipulation
    if (handleArrowAnnotationMove(event->pos())) {
        return;
    }

    // Handle polyline annotation dragging/vertex manipulation
    if (handlePolylineAnnotationMove(event->pos())) {
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

        // Handle emoji sticker drag/scale release
        if (handleEmojiStickerRelease()) {
            return;
        }

        // Handle arrow annotation drag/handle release
        if (handleArrowAnnotationRelease()) {
            return;
        }

        // Handle polyline annotation drag/vertex release
        if (handlePolylineAnnotationRelease()) {
            return;
        }

        // Handle region control widget release
        if (handleRegionControlWidgetRelease(event->pos())) {
            return;
        }

        // Handle selection release
        if (m_selectionManager->isSelecting()) {
            handleSelectionRelease(event->pos());
            emit updateRequested();
        }
        else if (m_selectionManager->isResizing()) {
            m_selectionManager->finishResize();
            emit updateRequested();
        }
        else if (m_selectionManager->isMoving()) {
            m_selectionManager->finishMove();
            emit updateRequested();
        }
        else if (state().isDrawing) {
            handleAnnotationRelease();
            emit updateRequested();
        }
        else if (shouldShowColorAndWidthWidget() &&
            m_colorAndWidthWidget->handleMouseRelease(event->pos())) {
            emit updateRequested();
        }
    }
}

// ============================================================================
// Mouse Press Helpers
// ============================================================================

bool RegionInputHandler::handleTextEditorPress(const QPoint& pos)
{
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

bool RegionInputHandler::handleToolbarPress(const QPoint& pos)
{
    int buttonIdx = m_toolbar->buttonAtPosition(pos);
    if (buttonIdx >= 0) {
        int buttonId = m_toolbar->buttonIdAt(buttonIdx);
        if (buttonId >= 0) {
            emit toolbarClickRequested(buttonId);
        }
        return true;
    }
    return false;
}

bool RegionInputHandler::handleRegionControlWidgetPress(const QPoint& pos)
{
    if (m_regionControlWidget && m_regionControlWidget->isVisible() &&
        m_regionControlWidget->handleMousePress(pos)) {
        emit updateRequested();
        return true;
    }
    return false;
}

bool RegionInputHandler::handleColorWidgetPress(const QPoint& pos)
{
    if (shouldShowColorAndWidthWidget()) {
        if (m_colorAndWidthWidget->handleClick(pos)) {
            emit updateRequested();
            return true;
        }
    }

    // Handle emoji picker clicks
    if (m_emojiPicker && m_emojiPicker->isVisible()) {
        if (m_emojiPicker->handleClick(pos)) {
            emit updateRequested();
            return true;
        }
    }

    return false;
}


bool RegionInputHandler::handleEmojiStickerAnnotationPress(const QPoint& pos)
{
    // First check if we're clicking on a gizmo handle of an already-selected emoji
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle != GizmoHandle::None) {
            if (handle == GizmoHandle::Body) {
                // Start dragging
                m_isEmojiDragging = true;
                m_emojiDragStart = pos;
                CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Moving);
            }
            else {
                // Start scaling (corner handles)
                m_isEmojiScaling = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartScale = emojiItem->scale();
                QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::GizmoHandle, static_cast<int>(handle));
            }
            if (m_parentWidget) {
                m_parentWidget->setFocus();
            }
            emit updateRequested();
            return true;
        }
    }

    // Check if clicking on a different emoji sticker
    int hitIndex = m_annotationLayer->hitTestEmojiSticker(pos);
    if (hitIndex < 0) {
        return false;
    }

    // Select this emoji sticker
    m_annotationLayer->setSelectedIndex(hitIndex);

    // Start dragging by default when clicking on the emoji body
    if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_isEmojiDragging = true;
            m_emojiDragStart = pos;
            CursorManager::instance().setInputStateForWidget(m_parentWidget, InputState::Moving);
        }
        else {
            // Corner handle - start scaling
            m_isEmojiScaling = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartScale = emojiItem->scale();
            QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            CursorManager::instance().setHoverTargetForWidget(m_parentWidget, HoverTarget::GizmoHandle, static_cast<int>(handle));
        }
    }

    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }
    emit updateRequested();
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
        qDebug() << "Starting annotation with tool:" << static_cast<int>(state().currentTool) << "at pos:" << pos;
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
        int hitIndex = m_multiRegionManager->hitTest(pos);
        if (hitIndex >= 0) {
            m_multiRegionManager->setActiveIndex(hitIndex);
            m_selectionManager->setSelectionRect(m_multiRegionManager->regionRect(hitIndex));

            auto handle = m_selectionManager->hitTestHandle(pos);
            if (handle != SelectionStateManager::ResizeHandle::None) {
                m_lastSelectionRect = m_selectionManager->selectionRect();
                m_selectionManager->startResize(pos, handle);
                emit updateRequested();
                return true;
            }

            if (m_selectionManager->hitTestMove(pos)) {
                m_lastSelectionRect = m_selectionManager->selectionRect();
                m_selectionManager->startMove(pos);
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
        m_selectionManager->startResize(pos, handle);
        emit updateRequested();
        return true;
    }

    if (m_selectionManager->hitTestMove(pos)) {
        m_lastSelectionRect = m_selectionManager->selectionRect();
        m_selectionManager->startMove(pos);
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
    m_selectionManager->startSelection(pos);
    m_startPoint = pos;
    state().currentPoint = pos;
    m_lastSelectionRect = QRect();
}

void RegionInputHandler::handleRightButtonPress(const QPoint& pos)
{
    if (state().multiRegionMode && m_multiRegionManager) {
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
        m_selectionManager->clearSelection();
        emit selectionCancelledByRightClick();
        return;
    }

    if (m_selectionManager->isComplete()) {
        if (state().isDrawing) {
            state().isDrawing = false;
            emit drawingStateChanged(false);
            m_toolManager->cancelDrawing();
            emit updateRequested();
            return;
        }
        if (m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
            m_selectionManager->cancelResizeOrMove();
            emit updateRequested();
            return;
        }
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


bool RegionInputHandler::handleEmojiStickerMove(const QPoint& pos)
{
    if (m_isEmojiDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* emojiItem = getSelectedEmojiStickerAnnotation();
        if (emojiItem) {
            QPoint delta = pos - m_emojiDragStart;
            emojiItem->moveBy(delta);
            m_emojiDragStart = pos;
            emit updateRequested();
            return true;
        }
    }

    if (m_isEmojiScaling && m_annotationLayer->selectedIndex() >= 0) {
        auto* emojiItem = getSelectedEmojiStickerAnnotation();
        if (emojiItem && m_emojiStartDistance > 0) {
            QPointF delta = QPointF(pos) - m_emojiStartCenter;
            qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            qreal scaleFactor = currentDistance / m_emojiStartDistance;
            qreal newScale = m_emojiStartScale * scaleFactor;
            // setScale already clamps to kMinScale/kMaxScale
            emojiItem->setScale(newScale);
            emit updateRequested();
            return true;
        }
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

void RegionInputHandler::handleSelectionMove(const QPoint& pos)
{
    state().highlightedWindowRect = QRect();
    state().hasDetectedWindow = false;
    emit detectionCleared();
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
        // Update toolbar hover first (toolbar always takes priority)
        m_toolbar->updateHoveredButton(pos);
        int hoveredButton = m_toolbar->hoveredButton();

        if (hoveredButton >= 0) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::ToolbarButton);
            return;
        }
        if (m_toolbar->contains(pos)) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Toolbar);
            return;
        }

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

        // Check color/width widget before falling back to CrossCursor
        if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->contains(pos)) {
            m_colorAndWidthWidget->handleMouseMove(pos, buttons & Qt::LeftButton);
            m_colorAndWidthWidget->updateHovered(pos);
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Widget);
            return;
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

    // Check text annotation hover
    if (m_annotationLayer->hitTestText(pos) >= 0) {
        cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Annotation);
        return;
    }

    // Check emoji sticker gizmo handles (only for selected emoji)
    if (state().currentTool != ToolId::EmojiSticker) {
        if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
            GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
            if (handle != GizmoHandle::None) {
                cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::GizmoHandle, static_cast<int>(handle));
                return;
            }
        }
    }

    // Check Arrow gizmo handles (for selected arrows)
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        if (handle != GizmoHandle::None) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    } else {
        // Check hover on unselected arrow (show move cursor)
        if (m_annotationLayer->hitTestArrow(pos) >= 0) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Annotation);
            return;
        }
    }

    // Check Polyline gizmo handles (for selected polylines)
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            // Vertex handle - use GizmoHandle type (will show CrossCursor via CursorManager)
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::GizmoHandle, static_cast<int>(GizmoHandle::ArrowStart));
            return;
        } else if (vertexIndex == -1) {
            // Body hit - show move cursor
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Annotation);
            return;
        }
    } else {
        // Check hover on unselected polyline
        if (m_annotationLayer->hitTestPolyline(pos) >= 0) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Annotation);
            return;
        }
    }

    // Update region control widget (radius + aspect ratio)
    if (m_regionControlWidget && m_regionControlWidget->isVisible()) {
        if (m_regionControlWidget->handleMouseMove(pos, buttons & Qt::LeftButton)) {
            emit updateRequested();
        }
        if (m_regionControlWidget->contains(pos)) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Widget);
            return;
        }
    }

    const bool colorAndWidthVisible = shouldShowColorAndWidthWidget();

    // Update color/width widget
    if (colorAndWidthVisible) {
        if (m_colorAndWidthWidget->handleMouseMove(pos, buttons & Qt::LeftButton)) {
            emit updateRequested();
        }
        if (m_colorAndWidthWidget->updateHovered(pos)) {
            emit updateRequested();
        }
        if (m_colorAndWidthWidget->contains(pos)) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Widget);
            return;
        }
    }

    // Check emoji picker
    if (m_emojiPicker && m_emojiPicker->isVisible()) {
        if (m_emojiPicker->updateHoveredEmoji(pos)) {
            emit updateRequested();
        }
        if (m_emojiPicker->contains(pos)) {
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Widget);
            return;
        }
    }

    // Update toolbar hover
    m_toolbar->updateHoveredButton(pos);
    if (m_toolbar->hoveredButton() >= 0) {
        cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::ToolbarButton);
        return;
    }
    if (m_toolbar->contains(pos)) {
        cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Toolbar);
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
            cm.setHoverTargetForWidget(m_parentWidget,HoverTarget::Annotation);
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
        if (hasRenderableSelection && m_toolbar) {
            m_toolbar->setViewportWidth(m_parentWidget->width());
            m_toolbar->setPositionForSelection(m_lastSelectionRect, m_parentWidget->height());
            m_lastToolbarRect = m_toolbar->boundingRect();
        }
        else {
            m_lastToolbarRect = QRect();
        }
        if (hasRenderableSelection && m_regionControlWidget) {
            const QRect dimensionInfoRect =
                m_dirtyRegionPlanner.dimensionInfoRectForSelection(m_lastSelectionRect);
            m_regionControlWidget->updatePosition(
                dimensionInfoRect, m_parentWidget->width(), m_parentWidget->height());
            m_lastRegionControlRect = m_regionControlWidget->boundingRect();
        }
        else {
            m_lastRegionControlRect = QRect();
        }

        m_parentWidget->update();  // Full repaint for first frame
        return;
    }

    if (m_selectionManager->isSelecting() || m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
        const QRect currentSelectionRect = m_selectionManager->selectionRect().normalized();
        const QRect currentMagnifierRect = m_dirtyRegionPlanner.magnifierRectForCursor(
            state().currentPoint, m_parentWidget->size());
        QRect currentToolbarRect;
        QRect currentRegionControlRect;
        const bool hasRenderableSelection = m_selectionManager->hasSelection() && currentSelectionRect.isValid();

        if (hasRenderableSelection && m_toolbar) {
            // Predict next toolbar placement from the latest selection rect instead of reusing stale paint geometry.
            m_toolbar->setViewportWidth(m_parentWidget->width());
            m_toolbar->setPositionForSelection(currentSelectionRect, m_parentWidget->height());
            currentToolbarRect = m_toolbar->boundingRect();
        }

        if (hasRenderableSelection && m_regionControlWidget) {
            // RegionControlWidget is positioned from the dimension panel anchor during paint;
            // mirror that anchor here so dirty-region planning includes next-frame placement.
            const QRect currentDimensionInfoRect =
                m_dirtyRegionPlanner.dimensionInfoRectForSelection(currentSelectionRect);
            m_regionControlWidget->updatePosition(
                currentDimensionInfoRect, m_parentWidget->width(), m_parentWidget->height());
            currentRegionControlRect = m_regionControlWidget->boundingRect();
        }

        SelectionDirtyRegionPlanner::SelectionDragParams params;
        params.currentSelectionRect = currentSelectionRect;
        params.lastSelectionRect = m_lastSelectionRect;
        params.currentToolbarRect = currentToolbarRect;
        params.lastToolbarRect = m_lastToolbarRect;
        params.currentRegionControlRect = currentRegionControlRect;
        params.lastRegionControlRect = m_lastRegionControlRect;
        params.currentMagnifierRect = currentMagnifierRect;
        params.lastMagnifierRect = m_lastMagnifierRect;
        params.viewportSize = m_parentWidget->size();
        const QRegion dirtyRegion = m_dirtyRegionPlanner.planSelectionDragRegion(params);

        m_lastSelectionRect = currentSelectionRect;
        m_lastMagnifierRect = currentMagnifierRect;
        m_lastToolbarRect = currentToolbarRect;
        m_lastRegionControlRect = currentRegionControlRect;
        m_lastCrosshairPoint = state().currentPoint;

        m_parentWidget->update(dirtyRegion);
    }
    else if (state().isDrawing) {
        if (m_updateThrottler->shouldUpdate(UpdateThrottler::ThrottleType::Annotation)) {
            m_updateThrottler->reset(UpdateThrottler::ThrottleType::Annotation);
            emit updateRequested();
        }
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
        params.viewportSize = m_parentWidget->size();
        const QRegion dirtyRegion = m_dirtyRegionPlanner.planHoverRegion(params);

        m_lastMagnifierRect = currentMagnifierRect;
        m_lastCrosshairPoint = state().currentPoint;

        m_parentWidget->update(dirtyRegion);
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
    if (m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseRelease(pos);
        return true;
    }
    return false;
}

bool RegionInputHandler::handleEmojiStickerRelease()
{
    if (m_isEmojiDragging || m_isEmojiScaling) {
        m_isEmojiDragging = false;
        m_isEmojiScaling = false;
        m_activeEmojiHandle = GizmoHandle::None;
        // Reset input state to let hover logic take over (consistent with arrow/polyline)
        CursorManager::instance().setInputStateForWidget(m_parentWidget,InputState::Idle);
        return true;
    }
    return false;
}

bool RegionInputHandler::handleRegionControlWidgetRelease(const QPoint& pos)
{
    if (m_regionControlWidget && m_regionControlWidget->isVisible() &&
        m_regionControlWidget->handleMouseRelease(pos)) {
        emit updateRequested();
        return true;
    }
    return false;
}

void RegionInputHandler::handleSelectionRelease(const QPoint& pos)
{
    QRect sel = m_selectionManager->selectionRect();
    if (state().multiRegionMode) {
        if (sel.width() > 5 && sel.height() > 5) {
            m_selectionManager->finishSelection();
            emit selectionFinished();
            qDebug() << "RegionInputHandler: Multi-region selection complete via drag";
        }
        else if (state().hasDetectedWindow && state().highlightedWindowRect.isValid()) {
            m_selectionManager->setFromDetectedWindow(state().highlightedWindowRect);
            emit selectionFinished();
            qDebug() << "RegionInputHandler: Multi-region selection via detected window";

            state().highlightedWindowRect = QRect();
            state().hasDetectedWindow = false;
            emit detectionCleared();
        }
        return;
    }

    if (sel.width() > 5 && sel.height() > 5) {
        m_selectionManager->finishSelection();
        emit selectionFinished();
        qDebug() << "RegionInputHandler: Selection complete via drag";
    }
    else if (state().hasDetectedWindow && state().highlightedWindowRect.isValid()) {
        m_selectionManager->setFromDetectedWindow(state().highlightedWindowRect);
        emit selectionFinished();
        qDebug() << "RegionInputHandler: Selection complete via detected window";

        state().highlightedWindowRect = QRect();
        state().hasDetectedWindow = false;
        emit detectionCleared();
    }
    else {
        emit fullScreenSelectionRequested();
        emit selectionFinished();
        qDebug() << "RegionInputHandler: Click without drag - selecting full screen";
    }
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

bool RegionInputHandler::shouldShowColorAndWidthWidget() const
{
    if (!m_colorAndWidthWidget) return false;
    return m_colorAndWidthWidget->isVisible();
}

bool RegionInputHandler::isAnnotationTool(ToolId tool) const
{
    return ToolTraits::isAnnotationTool(tool);
}

// ============================================================================
// Arrow Annotation Handlers
// ============================================================================

ArrowAnnotation* RegionInputHandler::getSelectedArrowAnnotation() const
{
    if (!m_annotationLayer) return nullptr;
    if (m_annotationLayer->selectedIndex() >= 0) {
        return dynamic_cast<ArrowAnnotation*>(m_annotationLayer->selectedItem());
    }
    return nullptr;
}

bool RegionInputHandler::handleArrowAnnotationPress(const QPoint& pos)
{
    // First check if we're clicking on a gizmo handle of an already-selected arrow
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        if (handle != GizmoHandle::None) {
            m_isArrowDragging = true;
            m_activeArrowHandle = handle;
            m_arrowDragStart = pos;

            auto& cm = CursorManager::instance();
            if (handle == GizmoHandle::Body) {
                cm.setInputStateForWidget(m_parentWidget, InputState::Moving);
            } else if (handle == GizmoHandle::ArrowControl) {
                cm.pushCursorForWidget(m_parentWidget, CursorContext::Selection, Qt::PointingHandCursor);
            } else {
                cm.setInputStateForWidget(m_parentWidget, InputState::Selecting);
            }

            if (m_parentWidget) {
                m_parentWidget->setFocus();
            }
            emit updateRequested();
            return true;
        }
    }

    // Check if clicking on a different arrow annotation
    int hitIndex = m_annotationLayer->hitTestArrow(pos);
    if (hitIndex < 0) {
        return false;
    }

    // Select this arrow annotation
    m_annotationLayer->setSelectedIndex(hitIndex);

    // Start dragging by default when clicking on the arrow body
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        m_isArrowDragging = true;
        m_activeArrowHandle = (handle != GizmoHandle::None) ? handle : GizmoHandle::Body;
        m_arrowDragStart = pos;
        CursorManager::instance().setInputStateForWidget(m_parentWidget,InputState::Moving);
    }

    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }
    emit updateRequested();
    return true;
}

bool RegionInputHandler::handleArrowAnnotationMove(const QPoint& pos)
{
    if (!m_isArrowDragging || m_annotationLayer->selectedIndex() < 0) {
        return false;
    }

    auto* arrowItem = getSelectedArrowAnnotation();
    if (!arrowItem) {
        return false;
    }

    QPoint delta = pos - m_arrowDragStart;

    switch (m_activeArrowHandle) {
    case GizmoHandle::ArrowStart:
        // Move start point directly
        arrowItem->setStart(arrowItem->start() + delta);
        break;

    case GizmoHandle::ArrowEnd:
        // Move end point directly
        arrowItem->setEnd(arrowItem->end() + delta);
        break;

    case GizmoHandle::ArrowControl:
        // User drags the curve midpoint (t=0.5), calculate actual Bézier control point
        // If B(0.5) = 0.25*P0 + 0.5*P1 + 0.25*P2, then:
        // P1 = 2*B(0.5) - 0.5*(P0 + P2) = 2*pos - 0.5*(start + end)
        {
            QPointF start = arrowItem->start();
            QPointF end = arrowItem->end();
            QPointF newControl = 2.0 * QPointF(pos) - 0.5 * (start + end);
            arrowItem->setControlPoint(newControl.toPoint());
        }
        break;

    case GizmoHandle::Body:
        // Move entire arrow
        arrowItem->moveBy(delta);
        break;

    default:
        return false;
    }

    m_arrowDragStart = pos;
    m_annotationLayer->invalidateCache();
    emit updateRequested();
    return true;
}

bool RegionInputHandler::handleArrowAnnotationRelease()
{
    if (!m_isArrowDragging) {
        return false;
    }

    m_isArrowDragging = false;
    m_activeArrowHandle = GizmoHandle::None;
    // Reset input state to let hover logic take over
    CursorManager::instance().setInputStateForWidget(m_parentWidget,InputState::Idle);
    return true;
}

// ============================================================================
// Polyline Annotation Handlers
// ============================================================================

PolylineAnnotation* RegionInputHandler::getSelectedPolylineAnnotation() const {
    if (!m_annotationLayer) return nullptr;
    if (m_annotationLayer->selectedIndex() < 0) return nullptr;
    return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->selectedItem());
}

bool RegionInputHandler::handlePolylineAnnotationPress(const QPoint& pos) {
    auto& cm = CursorManager::instance();

    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            // Vertex hit
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex;
            m_polylineDragStart = pos;
            cm.setInputStateForWidget(m_parentWidget,InputState::Selecting);
            if (m_parentWidget) m_parentWidget->setFocus();
            emit updateRequested();
            return true;
        } else if (vertexIndex == -1) {
             // Body hit
             m_isPolylineDragging = true;
             m_activePolylineVertexIndex = -1;
             m_polylineDragStart = pos;
             cm.setInputStateForWidget(m_parentWidget,InputState::Moving);
             if (m_parentWidget) m_parentWidget->setFocus();
             emit updateRequested();
             return true;
        }
    }

    // Check hit test for unselected items
    int hitIndex = m_annotationLayer->hitTestPolyline(pos);
    if (hitIndex >= 0) {
        m_annotationLayer->setSelectedIndex(hitIndex);

        // Default to dragging body on initial selection
        m_isPolylineDragging = true;
        m_activePolylineVertexIndex = -1;  // Start with body drag
        m_polylineDragStart = pos;

        // Check if we actually hit a vertex though, to be precise
        if (auto* polylineItem = getSelectedPolylineAnnotation()) {
             int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
             if (vertexIndex >= 0) {
                 m_activePolylineVertexIndex = vertexIndex;
                 cm.setInputStateForWidget(m_parentWidget,InputState::Selecting);
             } else {
                 cm.setInputStateForWidget(m_parentWidget,InputState::Moving);
             }
        }

        if (m_parentWidget) m_parentWidget->setFocus();
        emit updateRequested();
        return true;
    }
    return false;
}

bool RegionInputHandler::handlePolylineAnnotationMove(const QPoint& pos) {
    if (m_isPolylineDragging && m_annotationLayer->selectedIndex() >= 0) {
        auto* polylineItem = getSelectedPolylineAnnotation();
        if (polylineItem) {
             QPoint delta = pos - m_polylineDragStart;
             
             if (m_activePolylineVertexIndex >= 0) {
                 // Move specific vertex
                 QPoint currentPos = polylineItem->points()[m_activePolylineVertexIndex];
                 polylineItem->setPoint(m_activePolylineVertexIndex, currentPos + delta);
             } else {
                 // Move entire polyline
                 polylineItem->moveBy(delta);
             }
             
             m_polylineDragStart = pos;
             m_annotationLayer->invalidateCache();
             emit updateRequested();
             return true;
        }
    }
    return false;
}

bool RegionInputHandler::handlePolylineAnnotationRelease() {
    if (m_isPolylineDragging) {
        m_isPolylineDragging = false;
        m_activePolylineVertexIndex = -1;
        // Reset input state to let hover logic take over
        CursorManager::instance().setInputStateForWidget(m_parentWidget,InputState::Idle);
        emit updateRequested();
        return true;
    }
    return false;
}
