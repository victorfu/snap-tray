#ifndef REGIONINPUTHANDLER_H
#define REGIONINPUTHANDLER_H

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QCursor>
#include "TransformationGizmo.h"
#include "region/SelectionStateManager.h"

class QMouseEvent;
class QWidget;
class AnnotationLayer;
class ToolManager;
class ToolbarWidget;
class InlineTextEditor;
class TextAnnotationEditor;
class ColorAndWidthWidget;
class ColorPaletteWidget;
class EmojiPicker;
class RegionControlWidget;
class UpdateThrottler;
class TextAnnotation;
class EmojiStickerAnnotation;
class MultiRegionManager;

enum class ToolbarButton;

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
    void setToolbar(ToolbarWidget* toolbar);
    void setTextEditor(InlineTextEditor* editor);
    void setTextAnnotationEditor(TextAnnotationEditor* editor);
    void setColorAndWidthWidget(ColorAndWidthWidget* widget);
    void setColorPalette(ColorPaletteWidget* palette);
    void setEmojiPicker(EmojiPicker* picker);
    void setRegionControlWidget(RegionControlWidget* widget);
    void setMultiRegionManager(MultiRegionManager* manager);
    void setUpdateThrottler(UpdateThrottler* throttler);
    void setParentWidget(QWidget* widget);

    // Event handlers (called from RegionSelector)
    void handleMousePress(QMouseEvent* event);
    void handleMouseMove(QMouseEvent* event);
    void handleMouseRelease(QMouseEvent* event);

    // State accessors
    QPoint currentPoint() const { return m_currentPoint; }
    QPoint startPoint() const { return m_startPoint; }
    bool isDrawing() const { return m_isDrawing; }
    QRect lastMagnifierRect() const { return m_lastMagnifierRect; }

    // Configuration setters (call before event handling)
    void setCurrentTool(ToolbarButton tool);
    void setShowSubToolbar(bool show);
    void setHighlightedWindowRect(const QRect& rect);
    void setDetectedWindow(bool hasWindow, const QRect& bounds = QRect());
    void setAnnotationColor(const QColor& color);
    void setAnnotationWidth(int width);
    void setArrowStyle(int style);
    void setLineStyle(int style);
    void setShapeType(int type);
    void setShapeFillMode(int mode);
    void setMultiRegionMode(bool enabled) { m_multiRegionMode = enabled; }

signals:
    // Cursor management
    void cursorChangeRequested(Qt::CursorShape cursor);
    void toolCursorRequested();

    // Repaint requests
    void updateRequested();
    void updateRequested(const QRect& rect);

    // Action requests (need RegionSelector context)
    void toolbarClickRequested(int buttonId);
    void windowDetectionRequested(const QPoint& pos);
    void textReEditingRequested(int annotationIndex);
    void selectionFinished();
    void fullScreenSelectionRequested();

    // State change notifications
    void drawingStateChanged(bool isDrawing);
    void detectionCleared();

private:
    // Mouse press helpers
    bool handleTextEditorPress(const QPoint& pos);
    bool handleToolbarPress(const QPoint& pos);
    bool handleRegionControlWidgetPress(const QPoint& pos);
    bool handleColorWidgetPress(const QPoint& pos);
    bool handleGizmoPress(const QPoint& pos);
    bool handleTextAnnotationPress(const QPoint& pos);
    bool handleEmojiStickerAnnotationPress(const QPoint& pos);
    bool handleAnnotationToolPress(const QPoint& pos);
    bool handleSelectionToolPress(const QPoint& pos);
    void handleNewSelectionPress(const QPoint& pos);
    void handleRightButtonPress(const QPoint& pos);

    // Mouse move helpers
    bool handleTextEditorMove(const QPoint& pos);
    bool handleTextAnnotationMove(const QPoint& pos);
    bool handleEmojiStickerMove(const QPoint& pos);
    void handleWindowDetectionMove(const QPoint& pos);
    void handleSelectionMove(const QPoint& pos);
    void handleAnnotationMove(const QPoint& pos);
    void handleHoverMove(const QPoint& pos, Qt::MouseButtons buttons);
    void handleThrottledUpdate();

    // Mouse release helpers
    bool handleTextEditorRelease(const QPoint& pos);
    bool handleTextAnnotationRelease();
    bool handleEmojiStickerRelease();
    bool handleRegionControlWidgetRelease(const QPoint& pos);
    void handleSelectionRelease(const QPoint& pos);
    void handleAnnotationRelease();

    // Annotation helpers
    void startAnnotation(const QPoint& pos);
    void updateAnnotation(const QPoint& pos);
    void finishAnnotation();

    // Cursor helpers
    Qt::CursorShape getCursorForGizmoHandle(GizmoHandle handle) const;
    void updateCursorForHandle(SelectionStateManager::ResizeHandle handle);
    void emitCursorChangeIfNeeded(Qt::CursorShape cursor);

    // Selection helpers
    SelectionStateManager::ResizeHandle determineHandleFromOutsideClick(
        const QPoint& pos, const QRect& sel) const;
    void adjustEdgesToPosition(const QPoint& pos, SelectionStateManager::ResizeHandle handle);

    // Utility
    TextAnnotation* getSelectedTextAnnotation() const;
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation() const;
    bool shouldShowColorPalette() const;
    bool shouldShowColorAndWidthWidget() const;
    bool isAnnotationTool(ToolbarButton tool) const;

    // Dependencies (non-owning pointers)
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    ToolbarWidget* m_toolbar = nullptr;
    InlineTextEditor* m_textEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
    ColorAndWidthWidget* m_colorAndWidthWidget = nullptr;
    ColorPaletteWidget* m_colorPalette = nullptr;
    EmojiPicker* m_emojiPicker = nullptr;
    RegionControlWidget* m_regionControlWidget = nullptr;
    MultiRegionManager* m_multiRegionManager = nullptr;
    UpdateThrottler* m_updateThrottler = nullptr;
    QWidget* m_parentWidget = nullptr;

    // State
    QPoint m_currentPoint;
    QPoint m_startPoint;
    QPoint m_lastWindowDetectionPos;
    QRect m_lastSelectionRect;
    QRect m_lastMagnifierRect;
    QRect m_highlightedWindowRect;
    bool m_hasDetectedWindow = false;
    QRect m_detectedWindowBounds;
    ToolbarButton m_currentTool;
    bool m_isDrawing = false;
    bool m_showSubToolbar = true;

    // Annotation settings (synced from RegionSelector)
    QColor m_annotationColor;
    int m_annotationWidth = 3;
    int m_arrowStyle = 0;
    int m_lineStyle = 0;
    int m_shapeType = 0;
    int m_shapeFillMode = 0;
    bool m_multiRegionMode = false;

    // Emoji sticker transformation state
    bool m_isEmojiDragging = false;
    bool m_isEmojiScaling = false;
    GizmoHandle m_activeEmojiHandle = GizmoHandle::None;
    QPoint m_emojiDragStart;
    qreal m_emojiStartScale = 1.0;
    qreal m_emojiStartDistance = 0.0;
    QPointF m_emojiStartCenter;

    // Cursor batching - avoid redundant cursor change signals
    Qt::CursorShape m_lastEmittedCursor = Qt::ArrowCursor;

    // Constants
    static constexpr int WINDOW_DETECTION_MIN_DISTANCE_SQ = 64;  // 8px threshold squared (improved for high-DPI)
};

#endif // REGIONINPUTHANDLER_H
