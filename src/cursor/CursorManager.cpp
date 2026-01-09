#include "cursor/CursorManager.h"

#include <QPainter>
#include <QPixmap>
#include <algorithm>

#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/ToolId.h"

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
    }
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
    if (currentTool == ToolId::Mosaic) {
        int mosaicWidth = m_toolManager->width();
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

Qt::CursorShape CursorManager::cursorForResizeHandle(int handle)
{
    // Handle values match SelectionStateManager::ResizeHandle enum:
    // None=0, TopLeft=1, Top=2, TopRight=3, Left=4, Right=5,
    // BottomLeft=6, Bottom=7, BottomRight=8

    switch (handle) {
    case 1: // TopLeft
    case 8: // BottomRight
        return Qt::SizeFDiagCursor;
    case 3: // TopRight
    case 6: // BottomLeft
        return Qt::SizeBDiagCursor;
    case 2: // Top
    case 7: // Bottom
        return Qt::SizeVerCursor;
    case 4: // Left
    case 5: // Right
        return Qt::SizeHorCursor;
    default:
        return Qt::ArrowCursor;
    }
}

Qt::CursorShape CursorManager::cursorForEdge(int edge)
{
    // Edge values match ResizeHandler::Edge enum:
    // None=0, Left=1, Right=2, Top=3, Bottom=4,
    // TopLeft=5, TopRight=6, BottomLeft=7, BottomRight=8

    switch (edge) {
    case 1: // Left
    case 2: // Right
        return Qt::SizeHorCursor;
    case 3: // Top
    case 4: // Bottom
        return Qt::SizeVerCursor;
    case 5: // TopLeft
    case 8: // BottomRight
        return Qt::SizeFDiagCursor;
    case 6: // TopRight
    case 7: // BottomLeft
        return Qt::SizeBDiagCursor;
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

    // Only apply if changed (compare by shape for standard cursors)
    if (newCursor.shape() != m_lastAppliedCursor.shape() ||
        newCursor.pixmap().cacheKey() != m_lastAppliedCursor.pixmap().cacheKey()) {
        m_targetWidget->setCursor(newCursor);
        m_lastAppliedCursor = newCursor;
        emit cursorChanged(newCursor);
    }
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
        case HoverTarget::Annotation:
        case HoverTarget::ColorPalette:
        case HoverTarget::Widget:
            hoverCursor = Qt::PointingHandCursor;
            break;
        case HoverTarget::ResizeHandle:
            if (m_hoverHandleIndex >= 0) {
                hoverCursor = cursorForResizeHandle(m_hoverHandleIndex);
            }
            break;
        case HoverTarget::GizmoHandle:
            // Gizmo handles typically use pointing hand or specialized cursors
            hoverCursor = Qt::PointingHandCursor;
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
