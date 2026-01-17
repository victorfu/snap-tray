#include "cursor/CursorManager.h"

#include <QPainter>
#include <QPixmap>
#include <QDebug>
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
    if (m_targetWidget == widget) {
        return;
    }

    // Clean up previous widget
    if (m_targetWidget) {
        m_targetWidget->unsetCursor();
        disconnect(m_targetWidget, &QObject::destroyed, this, nullptr);
    }

    // Reset state and cursor stack when switching target widgets
    resetState();
    m_cursorStack.clear();

    m_targetWidget = widget;

    // Connect to widget's destroyed signal to clean up
    if (m_targetWidget) {
        connect(m_targetWidget, &QObject::destroyed, this, [this]() {
            m_targetWidget = nullptr;
            m_cursorStack.clear();
            resetState();
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
// Transaction Support
// ============================================================================

void CursorManager::beginTransaction()
{
    m_transactionDepth++;
}

void CursorManager::commitTransaction()
{
    if (m_transactionDepth > 0) {
        m_transactionDepth--;
    }
    if (m_transactionDepth == 0) {
        applyCursor();
    }
}

bool CursorManager::inTransaction() const
{
    return m_transactionDepth > 0;
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

// ============================================================================
// Private Methods
// ============================================================================

void CursorManager::applyCursor()
{
    // Skip if in transaction - cursor will be applied on commit
    if (m_transactionDepth > 0) {
        return;
    }

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
        case HoverTarget::SelectionBody:
            hoverCursor = Qt::SizeAllCursor;  // Draggable cursor for annotations/selection
            break;
        case HoverTarget::ResizeHandle:
            if (m_hoverHandleIndex >= 0) {
                auto handle = static_cast<SelectionStateManager::ResizeHandle>(m_hoverHandleIndex);
                hoverCursor = cursorForHandle(handle);
            }
            break;
        case HoverTarget::GizmoHandle:
            // Use appropriate cursor based on gizmo handle type
            if (m_hoverHandleIndex >= 0) {
                GizmoHandle handle = static_cast<GizmoHandle>(m_hoverHandleIndex);
                switch (handle) {
                case GizmoHandle::Body:
                    hoverCursor = Qt::SizeAllCursor;
                    break;
                case GizmoHandle::Rotation:
                    hoverCursor = Qt::PointingHandCursor;
                    break;
                case GizmoHandle::TopLeft:
                case GizmoHandle::BottomRight:
                    hoverCursor = Qt::SizeFDiagCursor;
                    break;
                case GizmoHandle::TopRight:
                case GizmoHandle::BottomLeft:
                    hoverCursor = Qt::SizeBDiagCursor;
                    break;
                default:
                    hoverCursor = Qt::ArrowCursor;
                    break;
                }
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

// ============================================================================
// Diagnostics (Debug only)
// ============================================================================

#ifdef QT_DEBUG
void CursorManager::dumpState() const
{
    qDebug() << "=== CursorManager State ===";
    qDebug() << "Target widget:" << (m_targetWidget ? m_targetWidget->objectName() : "none");
    qDebug() << "InputState:" << static_cast<int>(m_inputState);
    qDebug() << "HoverTarget:" << static_cast<int>(m_hoverTarget);
    qDebug() << "DragState:" << static_cast<int>(m_dragState);
    qDebug() << "HoverHandleIndex:" << m_hoverHandleIndex;
    qDebug() << "TransactionDepth:" << m_transactionDepth;
    qDebug() << "Global stack size:" << m_cursorStack.size();
    for (const auto& entry : m_cursorStack) {
        qDebug() << "  Context:" << static_cast<int>(entry.context)
                 << "Shape:" << entry.cursor.shape();
    }
    qDebug() << "Per-widget stacks:" << m_widgetCursorStacks.keys().size();
    for (auto it = m_widgetCursorStacks.begin(); it != m_widgetCursorStacks.end(); ++it) {
        qDebug() << "  Widget:" << (it.key() ? it.key()->objectName() : "null")
                 << "Stack size:" << it.value().size();
    }
}
#endif

// ============================================================================
// CursorGuard Implementation
// ============================================================================

CursorGuard::CursorGuard(CursorContext context, Qt::CursorShape cursor)
    : m_widget(nullptr)
    , m_context(context)
    , m_active(true)
{
    CursorManager::instance().pushCursor(context, cursor);
}

CursorGuard::CursorGuard(QWidget* widget, CursorContext context, Qt::CursorShape cursor)
    : m_widget(widget)
    , m_context(context)
    , m_active(true)
{
    CursorManager::instance().pushCursorForWidget(widget, context, cursor);
}

CursorGuard::~CursorGuard()
{
    if (m_active) {
        if (m_widget) {
            CursorManager::instance().popCursorForWidget(m_widget, m_context);
        } else {
            CursorManager::instance().popCursor(m_context);
        }
    }
}

CursorGuard::CursorGuard(CursorGuard&& other) noexcept
    : m_widget(other.m_widget)
    , m_context(other.m_context)
    , m_active(other.m_active)
{
    other.m_active = false;
}

CursorGuard& CursorGuard::operator=(CursorGuard&& other) noexcept
{
    if (this != &other) {
        // Pop existing cursor if active
        if (m_active) {
            if (m_widget) {
                CursorManager::instance().popCursorForWidget(m_widget, m_context);
            } else {
                CursorManager::instance().popCursor(m_context);
            }
        }

        m_widget = other.m_widget;
        m_context = other.m_context;
        m_active = other.m_active;
        other.m_active = false;
    }
    return *this;
}

void CursorGuard::release()
{
    m_active = false;
}
