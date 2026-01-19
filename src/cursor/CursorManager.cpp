#include "cursor/CursorManager.h"

#include <QPainter>
#include <QPixmap>
#include <algorithm>

#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/ToolId.h"
#include "TransformationGizmo.h"  // For GizmoHandle enum

CursorManager::CursorManager()
    : QObject(nullptr)
{
}

CursorManager& CursorManager::instance()
{
    static CursorManager instance;
    return instance;
}

void CursorManager::setTargetWidget(QWidget* widget)
{
    // Disconnect from previous widget if any
    if (m_targetWidget) {
        disconnect(m_targetWidget, &QObject::destroyed, this, nullptr);
    }

    m_targetWidget = widget;

    // Connect to widget's destroyed signal to clean up
    if (m_targetWidget) {
        connect(m_targetWidget, &QObject::destroyed, this, [this]() {
            m_targetWidget = nullptr;
            m_cursorStack.clear();
            // Reset state without triggering cursor update (no widget to update)
            m_inputState = InputState::Idle;
            m_hoverTarget = HoverTarget::None;
            m_dragState = DragState::None;
            m_hoverHandleIndex = -1;
        });
    }
}

void CursorManager::setToolManager(ToolManager* manager)
{
    if (m_toolManager) {
        disconnect(m_toolManager, nullptr, this, nullptr);
    }

    m_toolManager = manager;

    if (m_toolManager) {
        connect(m_toolManager, &ToolManager::toolChanged,
                this, &CursorManager::updateToolCursor);
        // Clean up when tool manager is destroyed
        connect(m_toolManager, &QObject::destroyed, this, [this]() {
            m_toolManager = nullptr;
        });
    }
}

// ============================================================================
// Cursor Stack Operations
// ============================================================================

void CursorManager::pushCursor(CursorContext context, const QCursor& cursor)
{
    // Remove existing entry for this context
    popCursor(context);

    // Add new entry
    m_cursorStack.append({context, cursor});

    // Sort by priority (highest last)
    std::sort(m_cursorStack.begin(), m_cursorStack.end());

    applyCursor();
}

void CursorManager::pushCursor(CursorContext context, Qt::CursorShape shape)
{
    pushCursor(context, QCursor(shape));
}

void CursorManager::popCursor(CursorContext context)
{
    auto it = std::remove_if(m_cursorStack.begin(), m_cursorStack.end(),
                             [context](const CursorEntry& entry) {
                                 return entry.context == context;
                             });

    if (it != m_cursorStack.end()) {
        m_cursorStack.erase(it, m_cursorStack.end());
        applyCursor();
    }
}

bool CursorManager::hasContext(CursorContext context) const
{
    return std::any_of(m_cursorStack.begin(), m_cursorStack.end(),
                       [context](const CursorEntry& entry) {
                           return entry.context == context;
                       });
}

void CursorManager::clearAll()
{
    m_cursorStack.clear();
    if (m_targetWidget) {
        m_targetWidget->setCursor(Qt::ArrowCursor);
        m_lastAppliedCursor = QCursor(Qt::ArrowCursor);
    }
}

void CursorManager::clearContexts(std::initializer_list<CursorContext> contexts)
{
    for (CursorContext context : contexts) {
        auto it = std::remove_if(m_cursorStack.begin(), m_cursorStack.end(),
                                  [context](const CursorEntry& entry) {
                                      return entry.context == context;
                                  });
        m_cursorStack.erase(it, m_cursorStack.end());
    }
    applyCursor();
}

// ============================================================================
// Per-Widget Cursor Management
// ============================================================================

void CursorManager::pushCursorForWidget(QWidget* widget, CursorContext context, const QCursor& cursor)
{
    if (!widget) {
        return;
    }

    // Connect to widget's destroyed signal if not already connected
    if (!m_widgetCursorStacks.contains(widget)) {
        connect(widget, &QObject::destroyed, this, [this, widget]() {
            m_widgetCursorStacks.remove(widget);
        });
    }

    // Get or create the cursor stack for this widget
    QVector<CursorEntry>& stack = m_widgetCursorStacks[widget];

    // Remove existing entry for this context
    auto it = std::remove_if(stack.begin(), stack.end(),
                              [context](const CursorEntry& entry) {
                                  return entry.context == context;
                              });
    stack.erase(it, stack.end());

    // Add new entry
    stack.append({context, cursor});

    // Sort by priority (highest last)
    std::sort(stack.begin(), stack.end());

    applyCursorForWidget(widget);
}

void CursorManager::pushCursorForWidget(QWidget* widget, CursorContext context, Qt::CursorShape shape)
{
    pushCursorForWidget(widget, context, QCursor(shape));
}

void CursorManager::popCursorForWidget(QWidget* widget, CursorContext context)
{
    if (!widget || !m_widgetCursorStacks.contains(widget)) {
        return;
    }

    QVector<CursorEntry>& stack = m_widgetCursorStacks[widget];
    auto it = std::remove_if(stack.begin(), stack.end(),
                              [context](const CursorEntry& entry) {
                                  return entry.context == context;
                              });

    if (it != stack.end()) {
        stack.erase(it, stack.end());
        applyCursorForWidget(widget);
    }
}

void CursorManager::clearAllForWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }

    m_widgetCursorStacks.remove(widget);
    widget->setCursor(Qt::ArrowCursor);
}

void CursorManager::applyCursorForWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }

    QCursor newCursor = effectiveCursorForWidget(widget);
    QCursor currentCursor = widget->cursor();

    // Only apply if changed
    if (newCursor.shape() != currentCursor.shape() ||
        newCursor.pixmap().cacheKey() != currentCursor.pixmap().cacheKey()) {
        widget->setCursor(newCursor);
    }
}

QCursor CursorManager::effectiveCursorForWidget(QWidget* widget) const
{
    if (!widget || !m_widgetCursorStacks.contains(widget)) {
        return QCursor(Qt::ArrowCursor);
    }

    const QVector<CursorEntry>& stack = m_widgetCursorStacks[widget];
    if (stack.isEmpty()) {
        return QCursor(Qt::ArrowCursor);
    }

    // Return the highest priority cursor (last in sorted list)
    return stack.last().cursor;
}

// ============================================================================
// Tool Cursor Integration
// ============================================================================

void CursorManager::updateToolCursor()
{
    if (!m_toolManager) {
        return;
    }

    ToolId currentTool = m_toolManager->currentTool();
    IToolHandler* handler = m_toolManager->currentHandler();

    // Special handling for mosaic tool - needs dynamic size
    // Use 2x width for mosaic cursor (UI shows half the actual drawing size)
    if (currentTool == ToolId::Mosaic) {
        int mosaicWidth = m_toolManager->width() * 2;
        pushCursor(CursorContext::Tool, createMosaicCursor(mosaicWidth));
        return;
    }

    // Use handler's cursor() method
    if (handler) {
        pushCursor(CursorContext::Tool, handler->cursor());
    } else {
        // Fallback to cross cursor for drawing tools
        pushCursor(CursorContext::Tool, Qt::CrossCursor);
    }
}

void CursorManager::updateMosaicCursor(int size)
{
    pushCursor(CursorContext::Tool, createMosaicCursor(size));
}

// ============================================================================
// Static Cursor Factories
// ============================================================================

QCursor CursorManager::createMosaicCursor(int size)
{
    int cursorSize = size + 4;  // Add margin for border
    QPixmap pixmap(cursorSize, cursorSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int center = cursorSize / 2;
    int halfSize = size / 2;

    // Draw semi-transparent fill
    painter.setBrush(QColor(255, 255, 255, 60));
    painter.setPen(Qt::NoPen);
    QRect innerRect(center - halfSize, center - halfSize, size, size);
    painter.drawRoundedRect(innerRect, 2, 2);

    // Draw light gray/white border for better visibility
    painter.setPen(QPen(QColor(220, 220, 220), 1.5, Qt::SolidLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(innerRect, 2, 2);

    // Draw darker inner outline for contrast on light backgrounds
    painter.setPen(QPen(QColor(100, 100, 100, 180), 0.5, Qt::SolidLine));
    QRect innerOutline = innerRect.adjusted(1, 1, -1, -1);
    painter.drawRoundedRect(innerOutline, 1, 1);

    painter.end();

    int hotspot = cursorSize / 2;
    return QCursor(pixmap, hotspot, hotspot);
}

Qt::CursorShape CursorManager::cursorForHandle(SelectionStateManager::ResizeHandle handle)
{
    using RH = SelectionStateManager::ResizeHandle;
    switch (handle) {
    case RH::TopLeft:
    case RH::BottomRight:
        return Qt::SizeFDiagCursor;
    case RH::TopRight:
    case RH::BottomLeft:
        return Qt::SizeBDiagCursor;
    case RH::Top:
    case RH::Bottom:
        return Qt::SizeVerCursor;
    case RH::Left:
    case RH::Right:
        return Qt::SizeHorCursor;
    case RH::None:
    default:
        return Qt::ArrowCursor;
    }
}

Qt::CursorShape CursorManager::cursorForEdge(ResizeHandler::Edge edge)
{
    using Edge = ResizeHandler::Edge;
    switch (edge) {
    case Edge::Left:
    case Edge::Right:
        return Qt::SizeHorCursor;
    case Edge::Top:
    case Edge::Bottom:
        return Qt::SizeVerCursor;
    case Edge::TopLeft:
    case Edge::BottomRight:
        return Qt::SizeFDiagCursor;
    case Edge::TopRight:
    case Edge::BottomLeft:
        return Qt::SizeBDiagCursor;
    case Edge::None:
    default:
        return Qt::ArrowCursor;
    }
}

Qt::CursorShape CursorManager::cursorForGizmoHandle(GizmoHandle handle)
{
    switch (handle) {
    case GizmoHandle::Body:
        return Qt::SizeAllCursor;
    case GizmoHandle::Rotation:
        return Qt::PointingHandCursor;
    case GizmoHandle::TopLeft:
    case GizmoHandle::BottomRight:
        return Qt::SizeFDiagCursor;
    case GizmoHandle::TopRight:
    case GizmoHandle::BottomLeft:
        return Qt::SizeBDiagCursor;
    case GizmoHandle::ArrowStart:
    case GizmoHandle::ArrowEnd:
        return Qt::CrossCursor;
    case GizmoHandle::ArrowControl:
        return Qt::PointingHandCursor;
    case GizmoHandle::None:
    default:
        return Qt::ArrowCursor;
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void CursorManager::applyCursor()
{
    if (!m_targetWidget) {
        return;
    }

    QCursor newCursor = effectiveCursor();
    QCursor currentCursor = m_targetWidget->cursor();

    // Only apply if changed (compare by shape for standard cursors)
    if (newCursor.shape() != currentCursor.shape() ||
        newCursor.pixmap().cacheKey() != currentCursor.pixmap().cacheKey()) {
        m_targetWidget->setCursor(newCursor);
        emit cursorChanged(newCursor);
    }
    m_lastAppliedCursor = newCursor;
}

QCursor CursorManager::effectiveCursor() const
{
    if (m_cursorStack.isEmpty()) {
        return QCursor(Qt::ArrowCursor);
    }

    // Return the highest priority cursor (last in sorted list)
    return m_cursorStack.last().cursor;
}

// ============================================================================
// State-Driven Cursor Management
// ============================================================================

void CursorManager::setInputState(InputState state)
{
    if (m_inputState != state) {
        m_inputState = state;
        updateCursorFromState();
    }
}

void CursorManager::setHoverTarget(HoverTarget target, int handleIndex)
{
    if (m_hoverTarget != target || m_hoverHandleIndex != handleIndex) {
        m_hoverTarget = target;
        m_hoverHandleIndex = handleIndex;
        updateCursorFromState();
    }
}

void CursorManager::setDragState(DragState state)
{
    if (m_dragState != state) {
        m_dragState = state;
        updateCursorFromState();
    }
}

void CursorManager::resetState()
{
    m_inputState = InputState::Idle;
    m_hoverTarget = HoverTarget::None;
    m_dragState = DragState::None;
    m_hoverHandleIndex = -1;
    updateCursorFromState();
}

void CursorManager::updateCursorFromState()
{
    // Priority: DragState > HoverTarget > InputState > Tool

    // 1. Drag state has highest priority
    if (m_dragState != DragState::None) {
        Qt::CursorShape dragCursor = Qt::ArrowCursor;
        switch (m_dragState) {
        case DragState::ToolbarDrag:
            dragCursor = Qt::ClosedHandCursor;
            break;
        case DragState::AnnotationDrag:
        case DragState::SelectionDrag:
            dragCursor = Qt::SizeAllCursor;
            break;
        case DragState::WidgetDrag:
            dragCursor = Qt::ClosedHandCursor;
            break;
        default:
            break;
        }
        pushCursor(CursorContext::Drag, dragCursor);
        return;
    } else {
        popCursor(CursorContext::Drag);
    }

    // 2. Hover target
    if (m_hoverTarget != HoverTarget::None) {
        Qt::CursorShape hoverCursor = Qt::ArrowCursor;
        switch (m_hoverTarget) {
        case HoverTarget::Toolbar:
            hoverCursor = Qt::OpenHandCursor;
            break;
        case HoverTarget::ToolbarButton:
        case HoverTarget::ColorPalette:
        case HoverTarget::Widget:
            hoverCursor = Qt::PointingHandCursor;
            break;
        case HoverTarget::Annotation:
            hoverCursor = Qt::SizeAllCursor;  // Draggable cursor for annotations
            break;
        case HoverTarget::ResizeHandle:
            if (m_hoverHandleIndex >= 0) {
                auto handle = static_cast<SelectionStateManager::ResizeHandle>(m_hoverHandleIndex);
                hoverCursor = cursorForHandle(handle);
            }
            break;
        case HoverTarget::GizmoHandle:
            if (m_hoverHandleIndex >= 0) {
                hoverCursor = cursorForGizmoHandle(static_cast<GizmoHandle>(m_hoverHandleIndex));
            }
            break;
        default:
            break;
        }
        pushCursor(CursorContext::Hover, hoverCursor);
        return;
    } else {
        popCursor(CursorContext::Hover);
    }

    // 3. Input state
    if (m_inputState != InputState::Idle) {
        Qt::CursorShape inputCursor = Qt::ArrowCursor;
        switch (m_inputState) {
        case InputState::Selecting:
            inputCursor = Qt::CrossCursor;
            break;
        case InputState::Moving:
            inputCursor = Qt::SizeAllCursor;
            break;
        case InputState::Resizing:
            // Resizing cursor should already be set by hover target
            inputCursor = Qt::SizeAllCursor;
            break;
        case InputState::Drawing:
            // Tool cursor should already be set
            return;
        default:
            break;
        }
        pushCursor(CursorContext::Selection, inputCursor);
        return;
    } else {
        popCursor(CursorContext::Selection);
    }

    // 4. Fall back to tool cursor (already managed separately)
    // Tool cursor is set via updateToolCursor() and persists in the stack
}
