#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QColor>
#include <QElapsedTimer>
#include <memory>
#include <optional>

#include "AnnotationLayer.h"
#include "ToolbarWidget.h"
#include "InlineTextEditor.h"
#include "WindowDetector.h"
#include "LoadingSpinnerRenderer.h"
#include "TransformationGizmo.h"
#include "TextFormattingState.h"

class QScreen;
class ColorPaletteWidget;
class LineWidthWidget;
class ColorAndWidthWidget;
class ColorPickerDialog;
class QCloseEvent;
class OCRManager;
class PencilStroke;
class MarkerStroke;
class ArrowAnnotation;
class RectangleAnnotation;
class EllipseAnnotation;
class MosaicAnnotation;
class MosaicStroke;
class StepBadgeAnnotation;
class AnnotationItem;

// Toolbar button types
enum class ToolbarButton {
    Selection = 0,
    Arrow,
    Pencil,
    Marker,
    Rectangle,
    Ellipse,
    Text,
    Mosaic,
    StepBadge,
    Eraser,
    Undo,
    Redo,
    Cancel,
    OCR,
    Pin,
    Record,
    Save,
    Copy,
    Count  // Total number of buttons
};

// Resize handle positions
enum class ResizeHandle {
    None = -1,
    TopLeft, Top, TopRight,
    Left, Center, Right,  // Center = move
    BottomLeft, Bottom, BottomRight
};

class RegionSelector : public QWidget
{
    Q_OBJECT

public:
    explicit RegionSelector(QWidget *parent = nullptr);
    ~RegionSelector();

    // 初始化指定螢幕的截圖 (由 CaptureManager 調用)
    void initializeForScreen(QScreen *screen);

    // 初始化指定螢幕並使用預設區域 (用於錄影取消後返回)
    void initializeWithRegion(QScreen *screen, const QRect &region);

    void setWindowDetector(WindowDetector *detector);

signals:
    void regionSelected(const QPixmap &screenshot, const QPoint &globalPosition);
    void selectionCancelled();
    void saveRequested(const QPixmap &screenshot);
    void copyRequested(const QPixmap &screenshot);
    void recordingRequested(const QRect &region, QScreen *screen);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void captureCurrentScreen();
    void drawOverlay(QPainter &painter);
    void drawSelection(QPainter &painter);
    void drawCrosshair(QPainter &painter);
    void drawMagnifier(QPainter &painter);
    void drawDimensionInfo(QPainter &painter);

    // Toolbar helpers
    void setupToolbarButtons();
    void handleToolbarClick(ToolbarButton button);
    QColor getToolbarIconColor(int buttonId, bool isActive, bool isHovered) const;

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Line width widget helpers (legacy)
    bool shouldShowLineWidthWidget() const;
    void onLineWidthChanged(int width);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;

    // Annotation settings persistence
    QColor loadAnnotationColor() const;
    void saveAnnotationColor(const QColor &color);
    int loadAnnotationWidth() const;
    void saveAnnotationWidth(int width);

    // Window detection drawing
    void drawDetectedWindow(QPainter &painter);
    void drawWindowHint(QPainter &painter, const QString &title);
    void updateWindowDetection(const QPoint &localPos);
    QPixmap getSelectedRegion();
    void copyToClipboard();
    void saveToFile();
    void finishSelection();

    // Annotation drawing helpers
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void startAnnotation(const QPoint &pos);
    void updateAnnotation(const QPoint &pos);
    void finishAnnotation();
    bool isAnnotationTool(ToolbarButton tool) const;
    void showTextInputDialog(const QPoint &pos);
    void placeStepBadge(const QPoint &pos);

    // Inline text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);
    void startTextReEditing(int annotationIndex);

    // Text formatting persistence
    TextFormattingState loadTextFormatting() const;
    void saveTextFormatting();

    // Text formatting signal handlers
    void onBoldToggled(bool enabled);
    void onItalicToggled(bool enabled);
    void onUnderlineToggled(bool enabled);
    void onFontSizeDropdownRequested(const QPoint& pos);
    void onFontFamilyDropdownRequested(const QPoint& pos);

    // Text annotation transformation helpers
    void startTextTransformation(const QPoint &pos, GizmoHandle handle);
    void updateTextTransformation(const QPoint &pos);
    void finishTextTransformation();

    // Selection resize/move helpers
    ResizeHandle getHandleAtPosition(const QPoint &pos);
    void updateResize(const QPoint &pos);
    void updateCursorForHandle(ResizeHandle handle);

    QPixmap m_backgroundPixmap;
    mutable QImage m_backgroundImageCache;  // Lazy-loaded cache for magnifier
    mutable bool m_backgroundImageCacheValid = false;  // Cache validity flag

    // Lazy accessor for background image (creates on first access)
    const QImage& getBackgroundImage() const;

    QPoint m_startPoint;
    QPoint m_currentPoint;
    QRect m_selectionRect;
    bool m_isSelecting;
    bool m_selectionComplete;
    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;
    bool m_showHexColor;  // true=HEX, false=RGB

    // Toolbar
    ToolbarWidget *m_toolbar;

    // Color palette
    ColorPaletteWidget *m_colorPalette;

    // Line width widget
    LineWidthWidget *m_lineWidthWidget;

    // Unified color and width widget
    ColorAndWidthWidget *m_colorAndWidthWidget;

    // Annotation layer and state
    AnnotationLayer *m_annotationLayer;
    ToolbarButton m_currentTool;
    QColor m_annotationColor;
    int m_annotationWidth;

    // In-progress annotation state
    bool m_isDrawing;
    QPoint m_drawStartPoint;
    QVector<QPointF> m_currentPath;

    // Temporary annotation objects for preview
    std::unique_ptr<PencilStroke> m_currentPencil;
    std::unique_ptr<MarkerStroke> m_currentMarker;
    std::unique_ptr<ArrowAnnotation> m_currentArrow;
    std::unique_ptr<RectangleAnnotation> m_currentRectangle;
    std::unique_ptr<EllipseAnnotation> m_currentEllipse;
    std::unique_ptr<MosaicStroke> m_currentMosaicStroke;

    // Eraser state
    QVector<QPoint> m_eraserPath;
    std::vector<ErasedItemsGroup::IndexedItem> m_erasedItems;  // Items erased during current stroke
    static const int ERASER_WIDTH = 20;

    // Selection resize/move state
    ResizeHandle m_activeHandle;
    bool m_isResizing;
    bool m_isMoving;
    bool m_isClosing;
    bool m_isDialogOpen;  // Prevents close during file dialog
    QPoint m_resizeStartPoint;
    QRect m_originalRect;

    // Window detection state
    WindowDetector *m_windowDetector;
    std::optional<DetectedElement> m_detectedWindow;
    QRect m_highlightedWindowRect;
    QPoint m_lastWindowDetectionPos;  // For throttling window detection
    static constexpr int WINDOW_DETECTION_MIN_DISTANCE_SQ = 25;  // 5px threshold squared

    // OCR state
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
    LoadingSpinnerRenderer *m_loadingSpinner;
    class QLabel *m_ocrToastLabel;
    class QTimer *m_ocrToastTimer;

    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);

    // Inline text editing
    InlineTextEditor *m_textEditor;

    // Text annotation dragging state
    bool m_isDraggingAnnotation = false;
    QPoint m_annotationDragStart;

    // Text annotation transformation state
    GizmoHandle m_activeGizmoHandle = GizmoHandle::None;
    bool m_isTransformingAnnotation = false;
    QPointF m_transformStartCenter;
    qreal m_transformStartRotation = 0.0;
    qreal m_transformStartScale = 1.0;
    qreal m_transformStartAngle = 0.0;
    qreal m_transformStartDistance = 0.0;

    // Text formatting state
    TextFormattingState m_textFormatting;
    int m_editingTextIndex = -1;  // -1 = creating new, >=0 = editing existing

    // Double-click detection for text re-editing
    QPoint m_lastTextClickPos;
    qint64 m_lastTextClickTime = 0;

    // Color picker dialog
    ColorPickerDialog *m_colorPickerDialog;

    // Magnifier performance optimization
    QElapsedTimer m_magnifierUpdateTimer;
    QPoint m_lastMagnifierPosition;
    QPixmap m_gridOverlayCache;
    QPixmap m_magnifierPixmapCache;
    QPoint m_cachedDevicePosition;
    bool m_magnifierCacheValid = false;
    static constexpr int MAGNIFIER_MIN_UPDATE_MS = 16;  // ~60fps cap
    static constexpr int MAGNIFIER_SIZE = 120;
    static constexpr int MAGNIFIER_GRID_COUNT = 15;

    // Update frequency control - per-operation throttling
    QElapsedTimer m_selectionUpdateTimer;
    QElapsedTimer m_annotationUpdateTimer;
    QElapsedTimer m_hoverUpdateTimer;
    static constexpr int SELECTION_UPDATE_MS = 8;    // 120fps for selection
    static constexpr int ANNOTATION_UPDATE_MS = 12;  // 80fps for drawing
    static constexpr int HOVER_UPDATE_MS = 32;       // 30fps for hover effects

    // Dirty region tracking for partial updates
    QRect m_lastSelectionRect;  // Previous selection rect for dirty region calculation
    QRect m_lastMagnifierRect;  // Previous magnifier rect

    void initializeMagnifierGridCache();
    void invalidateMagnifierCache();
};

#endif // REGIONSELECTOR_H
