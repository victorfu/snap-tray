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
class AnnotationLayer;
class ArrowAnnotation;
class PolylineAnnotation;
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
 * @brief Per-widget cursor manager for unified cursor handling.
 *
 * Provides centralized cursor management with priority-based context stack.
 * Each widget maintains its own cursor stack and state, allowing multiple
 * windows to coexist without cursor conflicts.
 *
 * Usage:
 * @code
 * auto& cm = CursorManager::instance();
 * cm.registerWidget(myWidget, toolManager);
 * cm.pushCursorForWidget(myWidget, CursorContext::Selection, Qt::SizeFDiagCursor);
 * // ... later
 * cm.popCursorForWidget(myWidget, CursorContext::Selection);
 * @endcode
 */
class CursorManager : public QObject {
    Q_OBJECT

public:
    static CursorManager& instance();

    // Disable copy
    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    // ========================================================================
    // Per-Widget Cursor Management
    // ========================================================================

    /**
     * @brief Register a widget for cursor management.
     *
     * Each widget maintains its own cursor stack and state. This allows
     * multiple windows to coexist without cursor conflicts.
     *
     * @param widget The widget to register
     * @param toolManager Optional tool manager for tool cursor integration
     */
    void registerWidget(QWidget* widget, ToolManager* toolManager = nullptr);

    /**
     * @brief Unregister a widget from cursor management.
     *
     * Clears all cursor state for the widget. Automatically called when
     * widget is destroyed.
     */
    void unregisterWidget(QWidget* widget);

    /**
     * @brief Set the tool manager for a specific widget.
     */
    void setToolManagerForWidget(QWidget* widget, ToolManager* manager);

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
     * @param widget The widget to clear cursors for
     * @param defaultCursor The cursor to reset to (default: CrossCursor for canvas widgets)
     */
    void clearAllForWidget(QWidget* widget, Qt::CursorShape defaultCursor = Qt::CrossCursor);

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
    // Per-Widget State-Driven Cursor Management
    // ========================================================================

    /**
     * @brief Set the input state for a specific widget.
     */
    void setInputStateForWidget(QWidget* widget, InputState state);

    /**
     * @brief Get the input state for a specific widget.
     */
    InputState inputStateForWidget(QWidget* widget) const;

    /**
     * @brief Set the hover target for a specific widget.
     */
    void setHoverTargetForWidget(QWidget* widget, HoverTarget target, int handleIndex = -1);

    /**
     * @brief Get the hover target for a specific widget.
     */
    HoverTarget hoverTargetForWidget(QWidget* widget) const;

    /**
     * @brief Set the drag state for a specific widget.
     */
    void setDragStateForWidget(QWidget* widget, DragState state);

    /**
     * @brief Get the drag state for a specific widget.
     */
    DragState dragStateForWidget(QWidget* widget) const;

    /**
     * @brief Update cursor based on current state for a specific widget.
     */
    void updateCursorFromStateForWidget(QWidget* widget);

    /**
     * @brief Reset all states to default for a specific widget.
     */
    void resetStateForWidget(QWidget* widget);

    /**
     * @brief Update tool cursor for a specific widget.
     */
    void updateToolCursorForWidget(QWidget* widget);

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

    // ========================================================================
    // Annotation Hit Testing (Unified Logic)
    // ========================================================================

    /**
     * @brief Result of annotation hit testing.
     */
    struct AnnotationHitResult {
        bool hit = false;                           // Whether any annotation was hit
        HoverTarget target = HoverTarget::None;     // Type of hover target
        int handleIndex = -1;                       // Handle/vertex index for gizmo targets
        Qt::CursorShape cursor = Qt::CrossCursor;   // Recommended cursor shape
    };

    /**
     * @brief Unified annotation hit testing for cursor updates.
     *
     * Tests arrows and polylines for hit, returning appropriate cursor information.
     * This eliminates duplicated logic in ScreenCanvas and PinWindow.
     *
     * @param pos The point to test (in annotation coordinates)
     * @param layer The annotation layer to test against
     * @param selectedArrow Currently selected arrow annotation (for gizmo handles), or nullptr
     * @param selectedPolyline Currently selected polyline annotation (for vertex handles), or nullptr
     * @return AnnotationHitResult with hit status, target type, and recommended cursor
     */
    static AnnotationHitResult hitTestAnnotations(
        const QPoint& pos,
        AnnotationLayer* layer,
        ArrowAnnotation* selectedArrow = nullptr,
        PolylineAnnotation* selectedPolyline = nullptr
    );

signals:
    /**
     * @brief Emitted when the effective cursor changes.
     */
    void cursorChanged(const QCursor& cursor);

private:
    CursorManager();
    ~CursorManager() override = default;

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

    // Per-widget cursor stacks for independent windows
    QHash<QWidget*, QVector<CursorEntry>> m_widgetCursorStacks;

    // Per-widget state management
    struct WidgetCursorState {
        InputState inputState = InputState::Idle;
        HoverTarget hoverTarget = HoverTarget::None;
        DragState dragState = DragState::None;
        int hoverHandleIndex = -1;
    };
    QHash<QWidget*, WidgetCursorState> m_widgetStates;
    QHash<QWidget*, ToolManager*> m_widgetToolManagers;
};

#endif // CURSORMANAGER_H
