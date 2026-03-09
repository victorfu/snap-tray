#include "cursor/CursorManager.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QtMath>
#include <algorithm>

#include "platform/WindowLevel.h"  // for forceNativeCrosshairCursor
#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/ToolId.h"
#include "tools/handlers/MosaicToolHandler.h"
#include "TransformationGizmo.h"  // For GizmoHandle enum
#include "annotations/AnnotationLayer.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"

namespace {
constexpr qreal kCursorRenderDprFloor = 2.0;
constexpr qreal kMosaicCursorOutlineWidth = 2.0;
constexpr int kMosaicCursorPadding = 4;
const QColor kCursorOutlineColor(0x6C, 0x5C, 0xE7);

qreal cursorRenderDpr()
{
    if (auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
        return std::max(kCursorRenderDprFloor, screen->devicePixelRatio());
    }
    if (auto* screen = QGuiApplication::primaryScreen()) {
        return std::max(kCursorRenderDprFloor, screen->devicePixelRatio());
    }
    if (qApp) {
        return std::max(kCursorRenderDprFloor, qApp->devicePixelRatio());
    }
    return kCursorRenderDprFloor;
}

QPixmap createCursorCanvas(const QSize& logicalSize, qreal dpr)
{
    const QSize physicalSize(
        qCeil(logicalSize.width() * dpr),
        qCeil(logicalSize.height() * dpr)
    );

    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}
}  // namespace

CursorManager::CursorManager()
    : QObject(nullptr)
{
}

CursorManager& CursorManager::instance()
{
    static CursorManager instance;
    return instance;
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

void CursorManager::clearAllForWidget(QWidget* widget, Qt::CursorShape defaultCursor)
{
    if (!widget) {
        return;
    }

    m_widgetCursorStacks.remove(widget);
    widget->setCursor(defaultCursor);
}

void CursorManager::setButtonCursor(QWidget* button)
{
    if (button) {
        instance().pushCursorForWidget(button, CursorContext::Hover, Qt::ArrowCursor);
    }
}

// ============================================================================
// Per-Widget Registration and State Management
// ============================================================================

void CursorManager::registerWidget(QWidget* widget, ToolManager* toolManager)
{
    if (!widget) {
        return;
    }

    // Initialize cursor stack if not already present
    if (!m_widgetCursorStacks.contains(widget)) {
        m_widgetCursorStacks[widget] = QVector<CursorEntry>();
        connect(widget, &QObject::destroyed, this, [this, widget]() {
            unregisterWidget(widget);
        });
    }

    // Initialize state
    m_widgetStates[widget] = WidgetCursorState();

    // Set tool manager if provided
    if (toolManager) {
        setToolManagerForWidget(widget, toolManager);
    }
}

void CursorManager::unregisterWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }

    m_widgetCursorStacks.remove(widget);
    m_widgetStates.remove(widget);

    // Disconnect from tool manager for this widget
    if (m_widgetToolManagers.contains(widget)) {
        ToolManager* tm = m_widgetToolManagers[widget];
        if (tm) {
            disconnect(tm, nullptr, this, nullptr);
        }
        m_widgetToolManagers.remove(widget);
    }
}

void CursorManager::setToolManagerForWidget(QWidget* widget, ToolManager* manager)
{
    if (!widget) {
        return;
    }

    // Disconnect from previous tool manager
    if (m_widgetToolManagers.contains(widget) && m_widgetToolManagers[widget]) {
        disconnect(m_widgetToolManagers[widget], nullptr, this, nullptr);
    }

    m_widgetToolManagers[widget] = manager;

    if (manager) {
        // Connect to tool changes - capture widget pointer
        connect(manager, &ToolManager::toolChanged, this, [this, widget]() {
            updateToolCursorForWidget(widget);
        });
        // Clean up when tool manager is destroyed
        connect(manager, &QObject::destroyed, this, [this, widget]() {
            if (m_widgetToolManagers.contains(widget)) {
                m_widgetToolManagers[widget] = nullptr;
            }
        });
    }
}

void CursorManager::setInputStateForWidget(QWidget* widget, InputState state)
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return;
    }

    WidgetCursorState& widgetState = m_widgetStates[widget];
    if (widgetState.inputState != state) {
        widgetState.inputState = state;
        updateCursorFromStateForWidget(widget);
    }
}

InputState CursorManager::inputStateForWidget(QWidget* widget) const
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return InputState::Idle;
    }
    return m_widgetStates[widget].inputState;
}

void CursorManager::setHoverTargetForWidget(QWidget* widget, HoverTarget target, int handleIndex)
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return;
    }

    WidgetCursorState& widgetState = m_widgetStates[widget];
    if (widgetState.hoverTarget != target || widgetState.hoverHandleIndex != handleIndex) {
        widgetState.hoverTarget = target;
        widgetState.hoverHandleIndex = handleIndex;
        updateCursorFromStateForWidget(widget);
    }
}

HoverTarget CursorManager::hoverTargetForWidget(QWidget* widget) const
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return HoverTarget::None;
    }
    return m_widgetStates[widget].hoverTarget;
}

void CursorManager::setDragStateForWidget(QWidget* widget, DragState state)
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return;
    }

    WidgetCursorState& widgetState = m_widgetStates[widget];
    if (widgetState.dragState != state) {
        widgetState.dragState = state;
        updateCursorFromStateForWidget(widget);
    }
}

DragState CursorManager::dragStateForWidget(QWidget* widget) const
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return DragState::None;
    }
    return m_widgetStates[widget].dragState;
}

void CursorManager::resetStateForWidget(QWidget* widget)
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return;
    }

    m_widgetStates[widget] = WidgetCursorState();
    updateCursorFromStateForWidget(widget);
}

void CursorManager::updateToolCursorForWidget(QWidget* widget)
{
    if (!widget || !m_widgetToolManagers.contains(widget)) {
        return;
    }

    ToolManager* toolManager = m_widgetToolManagers[widget];
    if (!toolManager) {
        return;
    }

    ToolId currentTool = toolManager->currentTool();
    IToolHandler* handler = toolManager->currentHandler();

    // For Mosaic tool, update the handler's width before getting cursor
    if (currentTool == ToolId::Mosaic) {
        if (auto* mosaicHandler = dynamic_cast<MosaicToolHandler*>(handler)) {
            mosaicHandler->setWidth(toolManager->width());
        }
    }

    // Use handler's cursor() method
    if (handler) {
        pushCursorForWidget(widget, CursorContext::Tool, handler->cursor());
    } else {
        // Fallback to cross cursor for drawing tools
        pushCursorForWidget(widget, CursorContext::Tool, Qt::CrossCursor);
    }
}

void CursorManager::reapplyCursorForWidget(QWidget* widget)
{
    applyCursorForWidget(widget, true);
}

void CursorManager::updateCursorFromStateForWidget(QWidget* widget)
{
    if (!widget || !m_widgetStates.contains(widget)) {
        return;
    }

    const WidgetCursorState& state = m_widgetStates[widget];

    // Priority: DragState > HoverTarget > InputState > Tool

    // 1. Drag state has highest priority
    if (state.dragState != DragState::None) {
        Qt::CursorShape dragCursor = Qt::CrossCursor;
        switch (state.dragState) {
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
        pushCursorForWidget(widget, CursorContext::Drag, dragCursor);
        return;
    } else {
        popCursorForWidget(widget, CursorContext::Drag);
    }

    // 2. Hover target
    if (state.hoverTarget != HoverTarget::None) {
        Qt::CursorShape hoverCursor = Qt::CrossCursor;
        switch (state.hoverTarget) {
        case HoverTarget::Toolbar:
        case HoverTarget::ToolbarButton:
        case HoverTarget::ColorPalette:
        case HoverTarget::Widget:
            hoverCursor = Qt::ArrowCursor;
            break;
        case HoverTarget::Annotation:
            hoverCursor = Qt::SizeAllCursor;
            break;
        case HoverTarget::ResizeHandle:
            if (state.hoverHandleIndex >= 0) {
                auto handle = static_cast<SelectionStateManager::ResizeHandle>(state.hoverHandleIndex);
                hoverCursor = cursorForHandle(handle);
            }
            break;
        case HoverTarget::GizmoHandle:
            if (state.hoverHandleIndex >= 0) {
                hoverCursor = cursorForGizmoHandle(static_cast<GizmoHandle>(state.hoverHandleIndex));
            }
            break;
        default:
            break;
        }
        pushCursorForWidget(widget, CursorContext::Hover, hoverCursor);
        return;
    } else {
        popCursorForWidget(widget, CursorContext::Hover);
    }

    // 3. Input state
    if (state.inputState != InputState::Idle) {
        Qt::CursorShape inputCursor = Qt::CrossCursor;
        switch (state.inputState) {
        case InputState::Selecting:
            inputCursor = Qt::CrossCursor;
            break;
        case InputState::Moving:
            inputCursor = Qt::SizeAllCursor;
            break;
        case InputState::Resizing:
            inputCursor = Qt::SizeAllCursor;
            break;
        case InputState::Drawing:
            // Tool cursor should already be set
            return;
        default:
            break;
        }
        pushCursorForWidget(widget, CursorContext::Selection, inputCursor);
        return;
    } else {
        popCursorForWidget(widget, CursorContext::Selection);
    }

    // 4. Fall back to tool cursor (already managed separately)
}

void CursorManager::applyCursorForWidget(QWidget* widget, bool force)
{
    if (!widget) {
        return;
    }

    QCursor newCursor = effectiveCursorForWidget(widget);
    QCursor currentCursor = widget->cursor();
    const bool cursorChanged =
        newCursor.shape() != currentCursor.shape() ||
        newCursor.pixmap().cacheKey() != currentCursor.pixmap().cacheKey();

    // macOS floating UI can change the native cursor without updating Qt's cached
    // widget cursor. Reapply while the widget owns the pointer so custom drawing
    // cursors (for example eraser/mosaic) do not get stuck as the default arrow.
#ifdef Q_OS_MAC
    if (force || cursorChanged || widget->underMouse()) {
#else
    if (force || cursorChanged) {
#endif
        widget->setCursor(newCursor);
    }

    // On macOS, always reinforce CrossCursor via native API.
    // Qt's cursor() returns a cached value that can desync from the actual
    // native macOS cursor (e.g., when mouse moves over text selection in other
    // apps, macOS sets IBeam but Qt doesn't know). By calling the native API
    // on every cursor application, we ensure the native cursor stays correct.
#ifdef Q_OS_MAC
    if (newCursor.shape() == Qt::CrossCursor) {
        forceNativeCrosshairCursor(widget);
    }
#endif
}

QCursor CursorManager::effectiveCursorForWidget(QWidget* widget) const
{
    if (!widget || !m_widgetCursorStacks.contains(widget)) {
        return QCursor(Qt::CrossCursor);
    }

    const QVector<CursorEntry>& stack = m_widgetCursorStacks[widget];
    if (stack.isEmpty()) {
        return QCursor(Qt::CrossCursor);
    }

    // Return the highest priority cursor (last in sorted list)
    return stack.last().cursor;
}

// ============================================================================
// Static Cursor Factories
// ============================================================================

QCursor CursorManager::createMosaicCursor(int size)
{
    const int boxSize = std::max(size, 2);
    const QSize logicalSize(
        boxSize + (kMosaicCursorPadding * 2),
        boxSize + (kMosaicCursorPadding * 2)
    );
    const qreal dpr = cursorRenderDpr();
    QPixmap pixmap = createCursorCanvas(logicalSize, dpr);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QPen pen(kCursorOutlineColor, kMosaicCursorOutlineWidth, Qt::SolidLine,
        Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Draw a single crisp square outline without any extra outer/inner guide lines.
    const qreal inset = kMosaicCursorPadding + (kMosaicCursorOutlineWidth / 2.0);
    const qreal rectExtent = std::max<qreal>(1.0, boxSize - kMosaicCursorOutlineWidth);
    painter.drawRect(QRectF(inset, inset, rectExtent, rectExtent));

    painter.end();

    const QPoint hotspot(logicalSize.width() / 2, logicalSize.height() / 2);
    return QCursor(pixmap, hotspot.x(), hotspot.y());
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
        return Qt::CrossCursor;
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
        return Qt::CrossCursor;
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
        return Qt::CrossCursor;
    }
}

// ============================================================================
// Annotation Hit Testing (Unified Logic)
// ============================================================================

CursorManager::AnnotationHitResult CursorManager::hitTestAnnotations(
    const QPoint& pos,
    AnnotationLayer* layer,
    EmojiStickerAnnotation* selectedEmoji,
    ArrowAnnotation* selectedArrow,
    PolylineAnnotation* selectedPolyline,
    bool enableEmojiGizmoHover)
{
    AnnotationHitResult result;

    if (!layer) {
        return result;
    }

    // 1. Check selected emoji sticker handles/body first.
    // Press handling in ScreenCanvas/PinWindow gives selected emoji interaction
    // precedence, so hover feedback must match that priority.
    if (enableEmojiGizmoHover && selectedEmoji) {
        GizmoHandle handle = TransformationGizmo::hitTest(selectedEmoji, pos);
        if (handle != GizmoHandle::None) {
            result.hit = true;
            result.target = HoverTarget::GizmoHandle;
            result.handleIndex = static_cast<int>(handle);
            result.cursor = cursorForGizmoHandle(handle);
            return result;
        }
    }

    // 2. Check selected arrow's gizmo handles (or hover on unselected arrows).
    if (selectedArrow) {
        GizmoHandle handle = TransformationGizmo::hitTest(selectedArrow, pos);
        if (handle != GizmoHandle::None) {
            result.hit = true;
            result.target = HoverTarget::GizmoHandle;
            result.handleIndex = static_cast<int>(handle);
            result.cursor = Qt::PointingHandCursor;
            return result;
        }
    } else {
        // Check hover on unselected arrows
        int hitIndex = layer->hitTestArrow(pos);
        if (hitIndex >= 0) {
            result.hit = true;
            result.target = HoverTarget::Annotation;
            result.handleIndex = hitIndex;
            result.cursor = Qt::SizeAllCursor;
            return result;
        }
    }

    // 3. Check text annotation hover.
    int textHitIndex = layer->hitTestText(pos);
    if (textHitIndex >= 0) {
        result.hit = true;
        result.target = HoverTarget::Annotation;
        result.handleIndex = textHitIndex;
        result.cursor = Qt::SizeAllCursor;
        return result;
    }

    // 4. Check selected polyline's vertex handles.
    if (selectedPolyline) {
        int vertexIndex = TransformationGizmo::hitTestVertex(selectedPolyline, pos);
        if (vertexIndex >= 0) {
            // Hit a vertex - can drag to reshape
            result.hit = true;
            result.target = HoverTarget::GizmoHandle;
            result.handleIndex = vertexIndex;
            result.cursor = Qt::CrossCursor;
            return result;
        } else if (vertexIndex == -1) {
            // Hit the body - can drag to move
            result.hit = true;
            result.target = HoverTarget::Annotation;
            result.handleIndex = -1;
            result.cursor = Qt::SizeAllCursor;
            return result;
        }
    } else {
        // Check hover on unselected polylines
        int hitIndex = layer->hitTestPolyline(pos);
        if (hitIndex >= 0) {
            result.hit = true;
            result.target = HoverTarget::Annotation;
            result.handleIndex = hitIndex;
            result.cursor = Qt::SizeAllCursor;
            return result;
        }
    }

    // No hit.
    return result;
}
