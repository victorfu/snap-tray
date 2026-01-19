#ifndef CURSORMANAGER_H
#define CURSORMANAGER_H

#include <QCursor>
#include <QObject>
#include <QWidget>
#include <QVector>
#include <QHash>
#include <initializer_list>

#include "region/SelectionStateManager.h"
#include "pinwindow/ResizeHandler.h"

class ToolManager;
enum class GizmoHandle;

/**
 * @brief Cursor context priority levels.
 *
 * Higher values have higher priority and will override lower values.
 * When a context is popped, the cursor reverts to the next highest priority.
 */
enum class CursorContext {
    Tool = 100,       // Tool cursor (lowest priority)
    Hover = 150,      // UI element hover
    Selection = 200,  // Selection/resize handles
    Drag = 300,       // Dragging operations
    Override = 500    // Force override (highest priority)
};

/**
 * @brief Input state for state-driven cursor management.
 */
enum class InputState {
    Idle,       // No active operation
    Selecting,  // Drawing selection rectangle
    Moving,     // Moving selection/annotation
    Resizing,   // Resizing selection
    Drawing     // Drawing with a tool
};

/**
 * @brief Hover target for determining cursor based on mouse position.
 */
enum class HoverTarget {
    None,           // Not hovering over any special element
    Toolbar,        // Hovering over toolbar background
    ToolbarButton,  // Hovering over toolbar button
    ResizeHandle,   // Hovering over resize handle
    GizmoHandle,    // Hovering over gizmo rotation/scale handle
    Annotation,     // Hovering over annotation
    ColorPalette,   // Hovering over color palette
    Widget          // Hovering over generic interactive widget
};

/**
 * @brief Drag state during drag operations.
 */
enum class DragState {
    None,           // Not dragging
    ToolbarDrag,    // Dragging toolbar
    AnnotationDrag, // Dragging annotation
    SelectionDrag,  // Dragging selection
    WidgetDrag      // Dragging generic widget
};

/**
 * @brief Global cursor manager for unified cursor handling.
 *
 * Provides centralized cursor management with priority-based context stack.
 * Integrates with ToolManager to automatically use IToolHandler::cursor().
 *
 * Usage:
 * @code
 * auto& cm = CursorManager::instance();
 * cm.setTargetWidget(myWidget);
 * cm.pushCursor(CursorContext::Selection, Qt::SizeFDiagCursor);
 * // ... later
 * cm.popCursor(CursorContext::Selection);
 * @endcode
 */
class CursorManager : public QObject {
    Q_OBJECT

public:
    static CursorManager& instance();

    // Disable copy
    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    /**
     * @brief Set the target widget for cursor changes.
     */
    void setTargetWidget(QWidget* widget);

    /**
     * @brief Get the current target widget.
     */
    QWidget* targetWidget() const { return m_targetWidget; }

    /**
     * @brief Set the tool manager for tool cursor integration.
     */
    void setToolManager(ToolManager* manager);

    // ========================================================================
    // Cursor Stack Operations
    // ========================================================================

    /**
     * @brief Push a cursor onto the context stack.
     *
     * If a cursor already exists for this context, it will be replaced.
     * The highest priority cursor in the stack will be applied.
     */
    void pushCursor(CursorContext context, const QCursor& cursor);

    /**
     * @brief Push a cursor with a standard Qt cursor shape.
     */
    void pushCursor(CursorContext context, Qt::CursorShape shape);

    /**
     * @brief Pop a cursor from the context stack.
     *
     * Removes the cursor for the specified context.
     * The next highest priority cursor will be applied.
     */
    void popCursor(CursorContext context);

    /**
     * @brief Check if a context has an active cursor.
     */
    bool hasContext(CursorContext context) const;

    /**
     * @brief Clear all cursor contexts and reset to default.
     */
    void clearAll();

    /**
     * @brief Clear specific cursor contexts.
     *
     * Useful for clearing multiple related contexts without resetting tool cursor.
     */
    void clearContexts(std::initializer_list<CursorContext> contexts);

    // ========================================================================
    // Per-Widget Cursor Management
    // ========================================================================

    /**
     * @brief Push a cursor for a specific widget.
     *
     * Each widget maintains its own cursor stack. This allows independent
     * windows (toolbars, floating panels) to manage cursors through
     * CursorManager while maintaining proper priority handling.
     *
     * @param widget The widget to set the cursor on
     * @param context The cursor context (priority level)
     * @param cursor The cursor to apply
     */
    void pushCursorForWidget(QWidget* widget, CursorContext context, const QCursor& cursor);
    void pushCursorForWidget(QWidget* widget, CursorContext context, Qt::CursorShape shape);

    /**
     * @brief Pop a cursor from a specific widget's stack.
     */
    void popCursorForWidget(QWidget* widget, CursorContext context);

    /**
     * @brief Clear all cursor contexts for a specific widget.
     */
    void clearAllForWidget(QWidget* widget);

    /**
     * @brief Set a pointing hand cursor for a button widget.
     *
     * Convenience method for buttons that need hover cursor feedback.
     * Uses the per-widget cursor system with Hover context.
     *
     * @param button The button widget to set cursor on
     */
    static void setButtonCursor(QWidget* button);

    // ========================================================================
    // Tool Cursor Integration
    // ========================================================================

    /**
     * @brief Update cursor based on current tool.
     *
     * Automatically gets cursor from IToolHandler::cursor().
     * Special handling for Mosaic tool (dynamic size).
     */
    void updateToolCursor();

    /**
     * @brief Update mosaic cursor with specific size.
     */
    void updateMosaicCursor(int size);

    // ========================================================================
    // State-Driven Cursor Management
    // ========================================================================

    /**
     * @brief Set the current input state.
     *
     * Input state represents the current operation mode (idle, selecting, etc.)
     */
    void setInputState(InputState state);

    /**
     * @brief Get the current input state.
     */
    InputState inputState() const { return m_inputState; }

    /**
     * @brief Set the current hover target.
     *
     * @param target The element being hovered over
     * @param handleIndex Optional index for handle-based targets (resize, gizmo)
     */
    void setHoverTarget(HoverTarget target, int handleIndex = -1);

    /**
     * @brief Get the current hover target.
     */
    HoverTarget hoverTarget() const { return m_hoverTarget; }

    /**
     * @brief Set the current drag state.
     */
    void setDragState(DragState state);

    /**
     * @brief Get the current drag state.
     */
    DragState dragState() const { return m_dragState; }

    /**
     * @brief Update cursor based on current state.
     *
     * Computes the appropriate cursor from InputState, HoverTarget, and DragState,
     * then applies it using the priority system.
     */
    void updateCursorFromState();

    /**
     * @brief Reset all states to default (Idle, None, None).
     */
    void resetState();

    // ========================================================================
    // Static Cursor Factories
    // ========================================================================

    /**
     * @brief Create a mosaic brush cursor.
     *
     * Creates a semi-transparent circular cursor with border and inner outline.
     */
    static QCursor createMosaicCursor(int size);

    /**
     * @brief Get cursor shape for resize handle (strongly-typed).
     *
     * @param handle The resize handle enum value
     * @return Appropriate cursor shape for the handle
     */
    static Qt::CursorShape cursorForHandle(SelectionStateManager::ResizeHandle handle);

    /**
     * @brief Get cursor shape for window edge (strongly-typed).
     *
     * @param edge The edge enum value
     * @return Appropriate cursor shape for the edge
     */
    static Qt::CursorShape cursorForEdge(ResizeHandler::Edge edge);

    /**
     * @brief Get cursor shape for gizmo handle (strongly-typed).
     *
     * @param handle The gizmo handle enum value
     * @return Appropriate cursor shape for the handle
     */
    static Qt::CursorShape cursorForGizmoHandle(GizmoHandle handle);

signals:
    /**
     * @brief Emitted when the effective cursor changes.
     */
    void cursorChanged(const QCursor& cursor);

private:
    CursorManager();
    ~CursorManager() override = default;

    /**
     * @brief Apply the highest priority cursor to the target widget.
     */
    void applyCursor();

    /**
     * @brief Get the current effective cursor.
     */
    QCursor effectiveCursor() const;

    struct CursorEntry {
        CursorContext context;
        QCursor cursor;

        bool operator<(const CursorEntry& other) const {
            return static_cast<int>(context) < static_cast<int>(other.context);
        }
    };

    /**
     * @brief Apply cursor to a specific widget based on its stack.
     */
    void applyCursorForWidget(QWidget* widget);

    /**
     * @brief Get effective cursor for a widget.
     */
    QCursor effectiveCursorForWidget(QWidget* widget) const;

    QVector<CursorEntry> m_cursorStack;
    QWidget* m_targetWidget = nullptr;
    ToolManager* m_toolManager = nullptr;
    QCursor m_lastAppliedCursor;

    // Per-widget cursor stacks for independent windows
    QHash<QWidget*, QVector<CursorEntry>> m_widgetCursorStacks;

    // State-driven cursor management
    InputState m_inputState = InputState::Idle;
    HoverTarget m_hoverTarget = HoverTarget::None;
    DragState m_dragState = DragState::None;
    int m_hoverHandleIndex = -1;
};

#endif // CURSORMANAGER_H
