#include "region/RegionInputHandler.h"
#include "region/SelectionStateManager.h"
#include "region/SelectionResizeHelper.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionControlWidget.h"
#include "region/MultiRegionManager.h"
#include "region/UpdateThrottler.h"
#include "region/MagnifierPanel.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"
#include "ToolbarWidget.h"
#include "InlineTextEditor.h"
#include "ColorAndWidthWidget.h"
#include "EmojiPicker.h"
#include "TransformationGizmo.h"
#include "RegionSelector.h"  // For isToolManagerHandledTool

#include <QMouseEvent>
#include <QWidget>
#include <QDateTime>
#include <QDebug>
#include <QTextEdit>

RegionInputHandler::RegionInputHandler(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolId::Selection)
{
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

void RegionInputHandler::setToolbar(ToolbarWidget* toolbar)
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

void RegionInputHandler::setColorAndWidthWidget(ColorAndWidthWidget* widget)
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

void RegionInputHandler::setCurrentTool(ToolId tool)
{
    m_currentTool = tool;
}

void RegionInputHandler::setShowSubToolbar(bool show)
{
    m_showSubToolbar = show;
}

void RegionInputHandler::setHighlightedWindowRect(const QRect& rect)
{
    m_highlightedWindowRect = rect;
}

void RegionInputHandler::setDetectedWindow(bool hasWindow)
{
    m_hasDetectedWindow = hasWindow;
}

void RegionInputHandler::setAnnotationColor(const QColor& color)
{
    m_annotationColor = color;
}

void RegionInputHandler::setAnnotationWidth(int width)
{
    m_annotationWidth = width;
}

void RegionInputHandler::setArrowStyle(int style)
{
    m_arrowStyle = style;
}

void RegionInputHandler::setLineStyle(int style)
{
    m_lineStyle = style;
}

void RegionInputHandler::setShapeType(int type)
{
    m_shapeType = type;
}

void RegionInputHandler::setShapeFillMode(int mode)
{
    m_shapeFillMode = mode;
}

// ============================================================================
// Main Event Handlers
// ============================================================================

void RegionInputHandler::handleMousePress(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_selectionManager->isComplete()) {
            // Handle inline text editing
            if (handleTextEditorPress(event->pos())) {
                return;
            }

            // Finalize polyline when clicking on UI elements
            auto finalizePolylineForUiClick = [&](const QPoint& pos) {
                if (m_currentTool == ToolId::Arrow && m_toolManager->isDrawing()) {
                    m_toolManager->handleDoubleClick(pos);
                    m_isDrawing = m_toolManager->isDrawing();
                    emit drawingStateChanged(m_isDrawing);
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

            // Check gizmo handles
            if (handleGizmoPress(event->pos())) {
                return;
            }

            // Check text annotations
            if (handleTextAnnotationPress(event->pos())) {
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

            // Handle Text tool
            QRect sel = m_selectionManager->selectionRect();
            if (m_currentTool == ToolId::Text) {
                m_textAnnotationEditor->startEditing(event->pos(),
                    m_parentWidget ? m_parentWidget->rect() : QRect(), m_annotationColor);
                return;
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
    m_currentPoint = event->pos();

    // Race condition recovery: mouse button was already pressed when window appeared
    // Qt doesn't fire mousePressEvent for already-pressed buttons, so we detect
    // this broken state here and synthesize the missing selection start
    if (!m_selectionManager->hasActiveSelection() &&
        (event->buttons() & Qt::LeftButton)) {
        qDebug() << "RegionInputHandler: Recovering from race condition - "
                 << "starting selection from mouseMoveEvent";
        handleNewSelectionPress(event->pos());
    }

    // Handle text editor in confirm mode
    if (handleTextEditorMove(event->pos())) {
        return;
    }

    // Handle text annotation transformation/dragging
    if (handleTextAnnotationMove(event->pos())) {
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
    else if (m_isDrawing) {
        handleAnnotationMove(event->pos());
    }
    else if (m_selectionManager->isComplete() ||
        (m_multiRegionMode && m_multiRegionManager && m_multiRegionManager->count() > 0)) {
        handleHoverMove(event->pos(), event->buttons());
    }

    // Throttled updates
    handleThrottledUpdate();
}

void RegionInputHandler::handleMouseRelease(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Handle text editor drag release
        if (handleTextEditorRelease(event->pos())) {
            return;
        }

        // Handle text annotation transformation/drag release
        if (handleTextAnnotationRelease()) {
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
        else if (m_isDrawing) {
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

bool RegionInputHandler::handleGizmoPress(const QPoint& pos)
{
    auto* textItem = getSelectedTextAnnotation();
    if (!textItem) {
        return false;
    }

    GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
    if (handle == GizmoHandle::None) {
        return false;
    }

    if (handle == GizmoHandle::Body) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_textAnnotationEditor->isDoubleClick(pos, now)) {
            emit textReEditingRequested(m_annotationLayer->selectedIndex());
            m_textAnnotationEditor->recordClick(QPoint(), 0);
            return true;
        }
        m_textAnnotationEditor->recordClick(pos, now);
        m_textAnnotationEditor->startDragging(pos);
    }
    else {
        m_textAnnotationEditor->startTransformation(pos, handle);
    }

    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }
    emit updateRequested();
    return true;
}

bool RegionInputHandler::handleTextAnnotationPress(const QPoint& pos)
{
    int hitIndex = m_annotationLayer->hitTestText(pos);
    if (hitIndex < 0) {
        return false;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Double-click on ANY text annotation triggers re-edit (no need to be pre-selected)
    if (m_textAnnotationEditor->isDoubleClick(pos, now)) {
        m_annotationLayer->setSelectedIndex(hitIndex);
        emit textReEditingRequested(hitIndex);
        m_textAnnotationEditor->recordClick(QPoint(), 0);
        return true;
    }
    m_textAnnotationEditor->recordClick(pos, now);

    // Single click: select and start dragging
    m_annotationLayer->setSelectedIndex(hitIndex);
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle == GizmoHandle::Body || handle == GizmoHandle::None) {
            m_textAnnotationEditor->startDragging(pos);
        }
        else {
            m_textAnnotationEditor->startTransformation(pos, handle);
        }
    }

    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }
    emit updateRequested();
    return true;
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
                emitCursorChangeIfNeeded(Qt::SizeAllCursor);
            }
            else {
                // Start scaling (corner handles)
                m_isEmojiScaling = true;
                m_activeEmojiHandle = handle;
                m_emojiStartCenter = emojiItem->center();
                m_emojiStartScale = emojiItem->scale();
                QPointF delta = QPointF(pos) - m_emojiStartCenter;
                m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
                emitCursorChangeIfNeeded(CursorManager::cursorForGizmoHandle(handle));
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
            emitCursorChangeIfNeeded(Qt::SizeAllCursor);
        }
        else {
            // Corner handle - start scaling
            m_isEmojiScaling = true;
            m_activeEmojiHandle = handle;
            m_emojiStartCenter = emojiItem->center();
            m_emojiStartScale = emojiItem->scale();
            QPointF delta = QPointF(pos) - m_emojiStartCenter;
            m_emojiStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());
            emitCursorChangeIfNeeded(CursorManager::cursorForGizmoHandle(handle));
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
    if (isAnnotationTool(m_currentTool) &&
        m_currentTool != ToolId::Selection &&
        sel.contains(pos)) {
        qDebug() << "Starting annotation with tool:" << static_cast<int>(m_currentTool) << "at pos:" << pos;
        startAnnotation(pos);
        return true;
    }
    return false;
}

bool RegionInputHandler::handleSelectionToolPress(const QPoint& pos)
{
    if (m_currentTool != ToolId::Selection) {
        return false;
    }

    if (m_multiRegionMode && m_multiRegionManager) {
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
    m_currentPoint = pos;
    m_lastSelectionRect = QRect();
}

void RegionInputHandler::handleRightButtonPress(const QPoint& pos)
{
    if (m_multiRegionMode && m_multiRegionManager) {
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
        if (m_isDrawing) {
            m_isDrawing = false;
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
        emitCursorChangeIfNeeded(Qt::CrossCursor);
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
            emitCursorChangeIfNeeded(Qt::SizeAllCursor);
        }
        else {
            emitCursorChangeIfNeeded(Qt::ArrowCursor);
        }
        emit updateRequested();
        return true;
    }
    return false;
}

bool RegionInputHandler::handleTextAnnotationMove(const QPoint& pos)
{
    if (m_textAnnotationEditor->isTransforming() && m_annotationLayer->selectedIndex() >= 0) {
        m_textAnnotationEditor->updateTransformation(pos);
        emit updateRequested();
        return true;
    }

    if (m_textAnnotationEditor->isDragging() && m_annotationLayer->selectedIndex() >= 0) {
        m_textAnnotationEditor->updateDragging(pos);
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
    m_highlightedWindowRect = QRect();
    m_hasDetectedWindow = false;
    emit detectionCleared();
    m_selectionManager->updateSelection(m_currentPoint);
}

void RegionInputHandler::handleAnnotationMove(const QPoint& pos)
{
    updateAnnotation(pos);

    if (m_currentTool == ToolId::Mosaic) {
        emit toolCursorRequested();
    }
}

void RegionInputHandler::handleHoverMove(const QPoint& pos, Qt::MouseButtons buttons)
{
    auto& cm = CursorManager::instance();

    // Multi-region mode cursor handling - MUST be checked FIRST and return early
    if (m_multiRegionMode && m_multiRegionManager && m_parentWidget) {
        // Update toolbar hover first (toolbar always takes priority)
        m_toolbar->updateHoveredButton(pos);
        int hoveredButton = m_toolbar->hoveredButton();

        if (hoveredButton >= 0) {
            cm.setHoverTarget(HoverTarget::ToolbarButton);
            return;
        }
        if (m_toolbar->contains(pos)) {
            cm.setHoverTarget(HoverTarget::Toolbar);
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
                cm.setHoverTarget(HoverTarget::ResizeHandle, static_cast<int>(handle));
                return;
            }
        }

        // Check color/width widget before falling back to CrossCursor
        if (shouldShowColorAndWidthWidget() && m_colorAndWidthWidget->contains(pos)) {
            m_colorAndWidthWidget->handleMouseMove(pos, buttons & Qt::LeftButton);
            m_colorAndWidthWidget->updateHovered(pos);
            cm.setHoverTarget(HoverTarget::Widget);
            return;
        }

        // Everything else in multi-region mode: reset hover and let input state control
        cm.setHoverTarget(HoverTarget::None);
        cm.setInputState(InputState::Selecting);
        return;
    }

    // === Single-region mode handling below ===
    // Check gizmo handles first (highest priority for selected items)
    if (auto* textItem = getSelectedTextAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(textItem, pos);
        if (handle != GizmoHandle::None) {
            cm.setHoverTarget(HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    }

    // Check text annotation hover
    if (m_annotationLayer->hitTestText(pos) >= 0) {
        cm.setHoverTarget(HoverTarget::Annotation);
        return;
    }

    // Check emoji sticker gizmo handles (only for selected emoji)
    if (m_currentTool != ToolId::EmojiSticker) {
        if (auto* emojiItem = getSelectedEmojiStickerAnnotation()) {
            GizmoHandle handle = TransformationGizmo::hitTest(emojiItem, pos);
            if (handle != GizmoHandle::None) {
                cm.setHoverTarget(HoverTarget::GizmoHandle, static_cast<int>(handle));
                return;
            }
        }
    }

    // Check Arrow gizmo handles (for selected arrows)
    if (auto* arrowItem = getSelectedArrowAnnotation()) {
        GizmoHandle handle = TransformationGizmo::hitTest(arrowItem, pos);
        if (handle != GizmoHandle::None) {
            cm.setHoverTarget(HoverTarget::GizmoHandle, static_cast<int>(handle));
            return;
        }
    } else {
        // Check hover on unselected arrow (show move cursor)
        if (m_annotationLayer->hitTestArrow(pos) >= 0) {
            cm.setHoverTarget(HoverTarget::Annotation);
            return;
        }
    }

    // Check Polyline gizmo handles (for selected polylines)
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            // Vertex handle - use GizmoHandle type (will show CrossCursor via CursorManager)
            cm.setHoverTarget(HoverTarget::GizmoHandle, static_cast<int>(GizmoHandle::ArrowStart));
            return;
        } else if (vertexIndex == -1) {
            // Body hit - show move cursor
            cm.setHoverTarget(HoverTarget::Annotation);
            return;
        }
    } else {
        // Check hover on unselected polyline
        if (m_annotationLayer->hitTestPolyline(pos) >= 0) {
            cm.setHoverTarget(HoverTarget::Annotation);
            return;
        }
    }

    // Update region control widget (radius + aspect ratio)
    if (m_regionControlWidget && m_regionControlWidget->isVisible()) {
        if (m_regionControlWidget->handleMouseMove(pos, buttons & Qt::LeftButton)) {
            emit updateRequested();
        }
        if (m_regionControlWidget->contains(pos)) {
            cm.setHoverTarget(HoverTarget::Widget);
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
            cm.setHoverTarget(HoverTarget::Widget);
            return;
        }
    }

    // Check emoji picker
    if (m_emojiPicker && m_emojiPicker->isVisible()) {
        if (m_emojiPicker->updateHoveredEmoji(pos)) {
            emit updateRequested();
        }
        if (m_emojiPicker->contains(pos)) {
            cm.setHoverTarget(HoverTarget::Widget);
            return;
        }
    }

    // Update toolbar hover
    m_toolbar->updateHoveredButton(pos);
    if (m_toolbar->hoveredButton() >= 0) {
        cm.setHoverTarget(HoverTarget::ToolbarButton);
        return;
    }
    if (m_toolbar->contains(pos)) {
        cm.setHoverTarget(HoverTarget::Toolbar);
        return;
    }

    // Check resize handles for selection tool
    if (m_currentTool == ToolId::Selection) {
        auto handle = m_selectionManager->hitTestHandle(pos);
        if (handle != SelectionStateManager::ResizeHandle::None) {
            cm.setHoverTarget(HoverTarget::ResizeHandle, static_cast<int>(handle));
            return;
        }

        // Check if hovering inside selection for move (displays SizeAllCursor)
        if (m_selectionManager->hitTestMove(pos)) {
            cm.setHoverTarget(HoverTarget::Annotation);
            return;
        }
    }

    // No special hover target - reset and let tool cursor show
    cm.setHoverTarget(HoverTarget::None);
    emit toolCursorRequested();

    // Handle eraser tool mouse move for hover highlighting
    if (m_currentTool == ToolId::Eraser && m_toolManager) {
        m_toolManager->handleMouseMove(pos);
    }
}

void RegionInputHandler::handleThrottledUpdate()
{
    if (!m_updateThrottler || !m_parentWidget) {
        return;
    }

    if (m_selectionManager->isSelecting() || m_selectionManager->isResizing() || m_selectionManager->isMoving()) {
        if (m_updateThrottler->shouldUpdate(UpdateThrottler::ThrottleType::Selection)) {
            m_updateThrottler->reset(UpdateThrottler::ThrottleType::Selection);

            // Calculate dirty rect for selection changes
            // Include both old and new selection rects, plus dimension info area
            QRect currentSelRect = m_selectionManager->selectionRect().normalized();

            // Expand to include selection border and handles (8px handles + 2px border)
            const int handleMargin = 12;
            QRect expandedCurrent = currentSelRect.adjusted(-handleMargin, -handleMargin, handleMargin, handleMargin);
            QRect expandedLast = m_lastSelectionRect.adjusted(-handleMargin, -handleMargin, handleMargin, handleMargin);

            QRect dirtyRect = expandedCurrent.united(expandedLast);

            // Include dimension info panel area (above selection)
            const int dimInfoHeight = 40;
            const int dimInfoWidth = 180;
            QRect dimInfoRect(currentSelRect.left(), currentSelRect.top() - dimInfoHeight - 10,
                              dimInfoWidth, dimInfoHeight);
            QRect lastDimInfoRect(m_lastSelectionRect.left(), m_lastSelectionRect.top() - dimInfoHeight - 10,
                                  dimInfoWidth, dimInfoHeight);
            dirtyRect = dirtyRect.united(dimInfoRect).united(lastDimInfoRect);

            // Include crosshair lines (full width/height thin strips)
            dirtyRect = dirtyRect.united(QRect(0, m_currentPoint.y() - 2, m_parentWidget->width(), 4));
            dirtyRect = dirtyRect.united(QRect(m_currentPoint.x() - 2, 0, 4, m_parentWidget->height()));

            // Include magnifier panel area
            const int panelWidth = MagnifierPanel::kWidth;
            const int totalHeight = MagnifierPanel::kHeight + 85;
            int panelX = m_currentPoint.x() - panelWidth / 2;
            panelX = qMax(10, qMin(panelX, m_parentWidget->width() - panelWidth - 10));
            int panelYBelow = m_currentPoint.y() + 25;
            int panelYAbove = m_currentPoint.y() - totalHeight - 25;
            QRect magRect(panelX - 5, qMin(panelYAbove, panelYBelow) - 5,
                          panelWidth + 10, totalHeight + qAbs(panelYBelow - panelYAbove) + 10);
            dirtyRect = dirtyRect.united(magRect).united(m_lastMagnifierRect);

            m_lastSelectionRect = currentSelRect;
            m_lastMagnifierRect = magRect;

            emit updateRequested(dirtyRect);
        }
    }
    else if (m_isDrawing) {
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
        if (m_updateThrottler->shouldUpdate(UpdateThrottler::ThrottleType::Magnifier)) {
            m_updateThrottler->reset(UpdateThrottler::ThrottleType::Magnifier);

            const int panelWidth = MagnifierPanel::kWidth;
            const int totalHeight = MagnifierPanel::kHeight + 85;
            int panelX = m_currentPoint.x() - panelWidth / 2;
            panelX = qMax(10, qMin(panelX, m_parentWidget->width() - panelWidth - 10));

            int panelYBelow = m_currentPoint.y() + 25;
            int panelYAbove = m_currentPoint.y() - totalHeight - 25;
            QRect belowRect(panelX - 5, panelYBelow - 5, panelWidth + 10, totalHeight + 10);
            QRect aboveRect(panelX - 5, panelYAbove - 5, panelWidth + 10, totalHeight + 10);
            QRect currentMagRect = belowRect.united(aboveRect);

            QRect dirtyRect = m_lastMagnifierRect.united(currentMagRect);
            dirtyRect = dirtyRect.united(QRect(0, m_currentPoint.y() - 3, m_parentWidget->width(), 6));
            dirtyRect = dirtyRect.united(QRect(m_currentPoint.x() - 3, 0, 6, m_parentWidget->height()));

            m_lastMagnifierRect = currentMagRect;
            emit updateRequested(dirtyRect);
        }
    }
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

bool RegionInputHandler::handleTextAnnotationRelease()
{
    if (m_textAnnotationEditor->isTransforming()) {
        m_textAnnotationEditor->finishTransformation();
        return true;
    }
    if (m_textAnnotationEditor->isDragging()) {
        m_textAnnotationEditor->finishDragging();
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
        CursorManager::instance().setInputState(InputState::Idle);
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
    if (m_multiRegionMode) {
        if (sel.width() > 5 && sel.height() > 5) {
            m_selectionManager->finishSelection();
            emitCursorChangeIfNeeded(Qt::ArrowCursor);
            emit selectionFinished();
            qDebug() << "RegionInputHandler: Multi-region selection complete via drag";
        }
        else if (m_hasDetectedWindow && m_highlightedWindowRect.isValid()) {
            m_selectionManager->setFromDetectedWindow(m_highlightedWindowRect);
            emitCursorChangeIfNeeded(Qt::ArrowCursor);
            emit selectionFinished();
            qDebug() << "RegionInputHandler: Multi-region selection via detected window";

            m_highlightedWindowRect = QRect();
            m_hasDetectedWindow = false;
            emit detectionCleared();
        }
        return;
    }

    if (sel.width() > 5 && sel.height() > 5) {
        m_selectionManager->finishSelection();
        emitCursorChangeIfNeeded(Qt::ArrowCursor);
        emit selectionFinished();
        qDebug() << "RegionInputHandler: Selection complete via drag";
    }
    else if (m_hasDetectedWindow && m_highlightedWindowRect.isValid()) {
        m_selectionManager->setFromDetectedWindow(m_highlightedWindowRect);
        emitCursorChangeIfNeeded(Qt::ArrowCursor);
        emit selectionFinished();
        qDebug() << "RegionInputHandler: Selection complete via detected window";

        m_highlightedWindowRect = QRect();
        m_hasDetectedWindow = false;
        emit detectionCleared();
    }
    else {
        emit fullScreenSelectionRequested();
        emitCursorChangeIfNeeded(Qt::ArrowCursor);
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
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->setColor(m_annotationColor);
        // Don't overwrite width for StepBadge - it uses a separate radius setting
        if (m_currentTool != ToolId::StepBadge) {
            m_toolManager->setWidth(m_annotationWidth);
        }
        m_toolManager->setArrowStyle(static_cast<LineEndStyle>(m_arrowStyle));
        m_toolManager->setLineStyle(static_cast<LineStyle>(m_lineStyle));
        m_toolManager->setShapeType(m_shapeType);
        m_toolManager->setShapeFillMode(m_shapeFillMode);

        m_toolManager->setCurrentTool(m_currentTool);
        m_toolManager->handleMousePress(pos);
        m_isDrawing = m_toolManager->isDrawing();

        if (!m_isDrawing && (m_currentTool == ToolId::StepBadge ||
            m_currentTool == ToolId::EmojiSticker)) {
            m_isDrawing = true;
        }
        emit drawingStateChanged(m_isDrawing);
        return;
    }
}

void RegionInputHandler::updateAnnotation(const QPoint& pos)
{
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->handleMouseMove(pos);
    }
}

void RegionInputHandler::finishAnnotation()
{
    if (isToolManagerHandledTool(m_currentTool)) {
        m_toolManager->handleMouseRelease(m_currentPoint);
        m_isDrawing = m_toolManager->isDrawing();
    }
    else {
        m_isDrawing = false;
    }
    emit drawingStateChanged(m_isDrawing);
}

// ============================================================================
// Cursor Helpers
// ============================================================================

void RegionInputHandler::updateCursorForHandle(SelectionStateManager::ResizeHandle handle)
{
    using ResizeHandle = SelectionStateManager::ResizeHandle;

    if (handle != ResizeHandle::None) {
        emitCursorChangeIfNeeded(CursorManager::cursorForHandle(handle));
        return;
    }

    // Handle::None - check for move or outside click
    if (m_selectionManager->hitTestMove(m_currentPoint)) {
        emitCursorChangeIfNeeded(Qt::SizeAllCursor);
    }
    else {
        QRect sel = m_selectionManager->selectionRect();
        ResizeHandle outsideHandle = SelectionResizeHelper::determineHandleFromOutsideClick(
            m_currentPoint, sel);
        emitCursorChangeIfNeeded(CursorManager::cursorForHandle(outsideHandle));
    }
}

void RegionInputHandler::emitCursorChangeIfNeeded(Qt::CursorShape cursor)
{
    if (cursor != m_lastEmittedCursor) {
        m_lastEmittedCursor = cursor;
        // Use CursorManager state-driven API for unified cursor management
        // Map cursor shape to appropriate state
        if (cursor == Qt::CrossCursor) {
            CursorManager::instance().setInputState(InputState::Selecting);
        } else if (cursor == Qt::SizeAllCursor) {
            CursorManager::instance().setInputState(InputState::Moving);
        } else {
            // For resize cursors, use hover target instead
            CursorManager::instance().setInputState(InputState::Resizing);
        }
    }
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
    switch (tool) {
    case ToolId::Pencil:
    case ToolId::Marker:
    case ToolId::Arrow:
    case ToolId::Shape:
    case ToolId::Text:
    case ToolId::Mosaic:
    case ToolId::Eraser:
    case ToolId::StepBadge:
    case ToolId::EmojiSticker:
        return true;
    default:
        return false;
    }
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

            if (handle == GizmoHandle::Body) {
                emitCursorChangeIfNeeded(Qt::SizeAllCursor);
            } else if (handle == GizmoHandle::ArrowControl) {
                emitCursorChangeIfNeeded(Qt::PointingHandCursor);
            } else {
                emitCursorChangeIfNeeded(Qt::CrossCursor);
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
        emitCursorChangeIfNeeded(Qt::SizeAllCursor);
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
    CursorManager::instance().setInputState(InputState::Idle);
    return true;
}

// ============================================================================
// Polyline Annotation Handlers
// ============================================================================

PolylineAnnotation* RegionInputHandler::getSelectedPolylineAnnotation() const {
    if (m_annotationLayer->selectedIndex() < 0) return nullptr;
    return dynamic_cast<PolylineAnnotation*>(m_annotationLayer->selectedItem());
}

bool RegionInputHandler::handlePolylineAnnotationPress(const QPoint& pos) {
    if (auto* polylineItem = getSelectedPolylineAnnotation()) {
        int vertexIndex = TransformationGizmo::hitTestVertex(polylineItem, pos);
        if (vertexIndex >= 0) {
            // Vertex hit
            m_isPolylineDragging = true;
            m_activePolylineVertexIndex = vertexIndex;
            m_polylineDragStart = pos;
            emitCursorChangeIfNeeded(Qt::CrossCursor); 
            if (m_parentWidget) m_parentWidget->setFocus();
            emit updateRequested();
            return true;
        } else if (vertexIndex == -1) {
             // Body hit
             m_isPolylineDragging = true;
             m_activePolylineVertexIndex = -1;
             m_polylineDragStart = pos;
             emitCursorChangeIfNeeded(Qt::SizeAllCursor);
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
                 emitCursorChangeIfNeeded(Qt::CrossCursor);
             } else {
                 emitCursorChangeIfNeeded(Qt::SizeAllCursor);
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
        CursorManager::instance().setInputState(InputState::Idle);
        emit updateRequested();
        return true;
    }
    return false;
}
