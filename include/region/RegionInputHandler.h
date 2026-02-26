#ifndef REGIONINPUTHANDLER_H
#define REGIONINPUTHANDLER_H

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QCursor>
#include <QTimer>
#include "TransformationGizmo.h"
#include "region/RegionInputState.h"
#include "region/SelectionStateManager.h"
#include "region/SelectionDirtyRegionPlanner.h"
#include "tools/ToolId.h"

class QMouseEvent;
class QWidget;
class AnnotationLayer;
class ToolManager;
class ToolbarCore;
class InlineTextEditor;
class TextAnnotationEditor;
class ToolOptionsPanel;
class EmojiPicker;
class RegionControlWidget;
class UpdateThrottler;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ShapeAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;
class MultiRegionManager;

/**
 * @brief Handles mouse input for RegionSelector.
 *
 * Processes mouse press, move, and release events, delegating to
 * appropriate handlers for selection, annotation, and UI interaction.
 * Uses signals for operations that require RegionSelector context.
 */
class RegionInputHandler : public QObject
{
    Q_OBJECT

public:
    explicit RegionInputHandler(QObject* parent = nullptr);

    // Dependency injection
    void setSelectionManager(SelectionStateManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setToolManager(ToolManager* manager);
    void setToolbar(ToolbarCore* toolbar);
    void setTextEditor(InlineTextEditor* editor);
    void setTextAnnotationEditor(TextAnnotationEditor* editor);
    void setColorAndWidthWidget(ToolOptionsPanel* widget);
    void setEmojiPicker(EmojiPicker* picker);
    void setRegionControlWidget(RegionControlWidget* widget);
    void setMultiRegionManager(MultiRegionManager* manager);
    void setUpdateThrottler(UpdateThrottler* throttler);
    void setParentWidget(QWidget* widget);
    void setSharedState(RegionInputState* state);

    // Reset dirty tracking state (call when starting a new capture)
    void resetDirtyTracking();

    // Event handlers (called from RegionSelector)
    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);

    // State accessors
    QPoint currentPoint() const { return m_state ? m_state->currentPoint : QPoint(); }
    QPoint startPoint() const { return m_startPoint; }
    bool isDrawing() const { return m_state && m_state->isDrawing; }
    QRect lastMagnifierRect() const { return m_lastMagnifierRect; }

signals:
    void toolCursorRequested();
    void currentPointUpdated(const QPoint& point);

    // Repaint requests
    void updateRequested();
    void updateRequested(const QRect& rect);

    // Action requests (need RegionSelector context)
    void toolbarClickRequested(int buttonId);
    void windowDetectionRequested(const QPoint& pos);
    void selectionFinished();
    void fullScreenSelectionRequested();
    void replaceSelectionCancelled();

    // State change notifications
    void drawingStateChanged(bool isDrawing);
    void detectionCleared(const QRect& previousHighlightRect);
    void selectionCancelledByRightClick();

private:
    // Mouse press helpers
    bool handleTextEditorPress(const QPoint& pos);
    bool handleToolbarPress(const QPoint& pos);
    bool handleRegionControlWidgetPress(const QPoint& pos);
    bool handleColorWidgetPress(const QPoint& pos);
    bool handleEmojiStickerAnnotationPress(const QPoint& pos);
    bool handleArrowAnnotationPress(const QPoint& pos);
    bool handleAnnotationToolPress(const QPoint& pos);
    bool handleSelectionToolPress(const QPoint& pos);
    void handleNewSelectionPress(const QPoint& pos);
    void handleRightButtonPress(const QPoint& pos);

    // Mouse move helpers
    bool handleTextEditorMove(const QPoint& pos);
    bool handleEmojiStickerMove(const QPoint& pos);
    bool handleArrowAnnotationMove(const QPoint& pos);
    void handleWindowDetectionMove(const QPoint& pos);
    void clearDetectionAndNotify();
    void handleSelectionMove(const QPoint& pos);
    void handleAnnotationMove(const QPoint& pos);
    void handleHoverMove(const QPoint& pos, Qt::MouseButtons buttons);
    void handleThrottledUpdate();
    void updateDragFramePump();
    void onDragFrameTick();
    QPoint currentCursorLocalPos() const;

    // Mouse release helpers
    bool handleTextEditorRelease(const QPoint& pos);
    bool handleEmojiStickerRelease();
    bool handleArrowAnnotationRelease();
    bool handleRegionControlWidgetRelease(const QPoint& pos);
    void handleSelectionRelease(const QPoint& pos);
    void handleAnnotationRelease();

    // Annotation helpers
    void startAnnotation(const QPoint& pos);
    void updateAnnotation(const QPoint& pos);
    void finishAnnotation();

    // Selection helpers
    SelectionStateManager::ResizeHandle determineHandleFromOutsideClick(
        const QPoint& pos, const QRect& sel) const;
    void adjustEdgesToPosition(const QPoint& pos, SelectionStateManager::ResizeHandle handle);

    // Utility
    TextBoxAnnotation* getSelectedTextAnnotation() const;
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation() const;
    ShapeAnnotation* getSelectedShapeAnnotation() const;
    ArrowAnnotation* getSelectedArrowAnnotation() const;
    bool shouldShowColorAndWidthWidget() const;
    bool isAnnotationTool(ToolId tool) const;
    RegionInputState& state();
    const RegionInputState& state() const;

    // Dependencies (non-owning pointers)
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    ToolbarCore* m_toolbar = nullptr;
    InlineTextEditor* m_textEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
    ToolOptionsPanel* m_colorAndWidthWidget = nullptr;
    EmojiPicker* m_emojiPicker = nullptr;
    RegionControlWidget* m_regionControlWidget = nullptr;
    MultiRegionManager* m_multiRegionManager = nullptr;
    UpdateThrottler* m_updateThrottler = nullptr;
    QWidget* m_parentWidget = nullptr;
    RegionInputState* m_state = nullptr;

    // State
    QPoint m_startPoint;
    QPoint m_lastWindowDetectionPos;
    QPoint m_pendingWindowClickStartPos;
    QPoint m_lastCrosshairPoint;  // Previous pointer position (retained for repaint tracking state)
    QRect m_pendingWindowClickRect;
    QRect m_lastSelectionRect;
    QRect m_lastMagnifierRect;
    QRect m_lastToolbarRect;
    QRect m_lastRegionControlRect;
    bool m_pendingWindowClickActive = false;

    // Emoji sticker transformation state
    bool m_isEmojiDragging = false;
    bool m_isEmojiScaling = false;
    bool m_isEmojiRotating = false;
    GizmoHandle m_activeEmojiHandle = GizmoHandle::None;
    QPoint m_emojiDragStart;
    qreal m_emojiStartScale = 1.0;
    qreal m_emojiStartDistance = 0.0;
    QPointF m_emojiStartCenter;
    qreal m_emojiStartRotation = 0.0;
    qreal m_emojiStartAngle = 0.0;

    // Arrow annotation transformation state
    bool m_isArrowDragging = false;
    GizmoHandle m_activeArrowHandle = GizmoHandle::None;
    QPoint m_arrowDragStart;

    // Polyline annotation transformation state
    bool m_isPolylineDragging = false;
    int m_activePolylineVertexIndex = -1; // -1 for body move, >= 0 for vertex move
    QPoint m_polylineDragStart;

    // Utility
    PolylineAnnotation* getSelectedPolylineAnnotation() const;
    bool handlePolylineAnnotationPress(const QPoint& pos);
    bool handlePolylineAnnotationMove(const QPoint& pos);
    bool handlePolylineAnnotationRelease();

    // Keyboard modifiers from current event (for angle snapping)
    Qt::KeyboardModifiers m_currentModifiers = Qt::NoModifier;

    // Constants
    static constexpr int WINDOW_DETECTION_MIN_DISTANCE_SQ = 64;  // 8px threshold squared (improved for high-DPI)
    static constexpr int WINDOW_CLICK_MAX_DISTANCE_SQ = 64;      // 8px drift still counts as click

    // Drag smoothing / repaint planning
    SelectionDirtyRegionPlanner m_dirtyRegionPlanner;
    QTimer m_dragFrameTimer;
};

#endif // REGIONINPUTHANDLER_H
