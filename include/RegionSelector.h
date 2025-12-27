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
#include <QSettings>
#include <memory>
#include <optional>

#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/StepBadgeAnnotation.h"
#include "ToolbarWidget.h"
#include "InlineTextEditor.h"
#include "WindowDetector.h"
#include "LoadingSpinnerRenderer.h"
#include "TransformationGizmo.h"
#include "TextFormattingState.h"
#include "tools/ToolId.h"
#include "tools/ToolManager.h"
#include "region/SelectionStateManager.h"
#include "region/MagnifierPanel.h"
#include "region/UpdateThrottler.h"
#include "region/TextAnnotationEditor.h"

class QScreen;
class ColorPaletteWidget;
class LineWidthWidget;
class ColorAndWidthWidget;
class ColorPickerDialog;
class QCloseEvent;
class OCRManager;
class TextAnnotation;

// ShapeType and ShapeFillMode are defined in annotations/ShapeAnnotation.h

// Toolbar button types
enum class ToolbarButton {
    Selection = 0,
    Arrow,
    Pencil,
    Marker,
    Shape,  // Unified shape tool (Rectangle + Ellipse)
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

// Helper to map ToolbarButton to ToolId
inline ToolId toolbarButtonToToolId(ToolbarButton btn) {
    switch (btn) {
    case ToolbarButton::Selection:  return ToolId::Selection;
    case ToolbarButton::Arrow:      return ToolId::Arrow;
    case ToolbarButton::Pencil:     return ToolId::Pencil;
    case ToolbarButton::Marker:     return ToolId::Marker;
    case ToolbarButton::Shape:      return ToolId::Shape;
    case ToolbarButton::Text:       return ToolId::Text;
    case ToolbarButton::Mosaic:     return ToolId::Mosaic;
    case ToolbarButton::StepBadge:  return ToolId::StepBadge;
    case ToolbarButton::Eraser:     return ToolId::Eraser;
    case ToolbarButton::Undo:       return ToolId::Undo;
    case ToolbarButton::Redo:       return ToolId::Redo;
    case ToolbarButton::Cancel:     return ToolId::Cancel;
    case ToolbarButton::OCR:        return ToolId::OCR;
    case ToolbarButton::Pin:        return ToolId::Pin;
    case ToolbarButton::Record:     return ToolId::Record;
    case ToolbarButton::Save:       return ToolId::Save;
    case ToolbarButton::Copy:       return ToolId::Copy;
    default:                        return ToolId::Selection;
    }
}

// Helper to check if a tool is handled by ToolManager
// (vs. custom handling like Text which has special UI needs)
inline bool isToolManagerHandledTool(ToolbarButton btn) {
    switch (btn) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Shape:
    case ToolbarButton::Mosaic:
    case ToolbarButton::StepBadge:
    case ToolbarButton::Eraser:
        return true;
    default:
        return false;
    }
}

class RegionSelector : public QWidget
{
    Q_OBJECT

public:
    explicit RegionSelector(QWidget *parent = nullptr);
    ~RegionSelector();

    // 初始化指定螢幕的截圖 (由 CaptureManager 調用)
    void initializeForScreen(QScreen *screen, const QPixmap &preCapture = QPixmap());

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
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void captureCurrentScreen();
    void drawOverlay(QPainter &painter);
    void drawDimmingOverlay(QPainter &painter, const QRect &clearRect, const QColor &dimColor);
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
    void syncColorToAllWidgets(const QColor &color);

    // Line width widget helpers (legacy)
    bool shouldShowLineWidthWidget() const;
    void onLineWidthChanged(int width);

    // Step Badge size handling
    void onStepBadgeSizeChanged(StepBadgeSize size);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;
    int toolWidthForCurrentTool() const;

    // Annotation settings persistence
    LineEndStyle loadArrowStyle() const;
    void saveArrowStyle(LineEndStyle style);
    void onArrowStyleChanged(LineEndStyle style);
    LineStyle loadLineStyle() const;
    void saveLineStyle(LineStyle style);
    void onLineStyleChanged(LineStyle style);

    // Window detection drawing
    void drawDetectedWindow(QPainter &painter);
    void drawWindowHint(QPainter &painter, const QString &title);
    void updateWindowDetection(const QPoint &localPos);
    QPixmap getSelectedRegion();
    void copyToClipboard();
    void saveToFile();
    void finishSelection();

    // Tool switching helpers
    void restoreStandardWidth();
    void saveEraserWidthAndClearHover();

    // Cursor helpers
    QCursor getMosaicCursor(int width);
    void setToolCursor();
    Qt::CursorShape getCursorForGizmoHandle(GizmoHandle handle) const;

    // Initialization helpers
    void setupScreenGeometry(QScreen* screen);

    // Annotation drawing helpers
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void startAnnotation(const QPoint &pos);
    void updateAnnotation(const QPoint &pos);
    void finishAnnotation();
    bool isAnnotationTool(ToolbarButton tool) const;
    void showTextInputDialog(const QPoint &pos);

    // Inline text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);
    void startTextReEditing(int annotationIndex);
    TextAnnotation* getSelectedTextAnnotation() const;
    bool handleInlineTextEditorPress(const QPoint& pos);

    // Text formatting persistence
    TextFormattingState loadTextFormatting() const;
    void saveTextFormatting();

    // Text formatting signal handlers
    void onFontSizeDropdownRequested(const QPoint& pos);
    void onFontFamilyDropdownRequested(const QPoint& pos);

    // Selection resize/move helpers
    void updateCursorForHandle(SelectionStateManager::ResizeHandle handle);

    // Settings helper (reduces QSettings instantiation)
    QSettings getSettings() const;

    // Screen coordinate conversion helpers
    QPoint localToGlobal(const QPoint& localPos) const;
    QPoint globalToLocal(const QPoint& globalPos) const;
    QRect localToGlobal(const QRect& localRect) const;
    QRect globalToLocal(const QRect& globalRect) const;

    QPixmap m_backgroundPixmap;
    mutable QImage m_backgroundImageCache;  // Lazy-loaded cache for magnifier
    mutable bool m_backgroundImageCacheValid = false;  // Cache validity flag

    // Lazy accessor for background image (creates on first access)
    const QImage& getBackgroundImage() const;

    QPoint m_startPoint;
    QPoint m_currentPoint;
    QScreen *m_currentScreen;

    // Selection state manager
    SelectionStateManager *m_selectionManager;
    qreal m_devicePixelRatio;

    // Toolbar
    ToolbarWidget *m_toolbar;

    // Color palette
    ColorPaletteWidget *m_colorPalette;

    // Line width widget
    LineWidthWidget *m_lineWidthWidget;

    // Unified color and width widget
    ColorAndWidthWidget *m_colorAndWidthWidget;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    ToolbarButton m_currentTool;
    QColor m_annotationColor;
    int m_annotationWidth;
    int m_eraserWidth = 20;  // Separate width for eraser tool (range: 5-100)
    int m_mosaicWidth = 30;  // Separate width for mosaic tool (range: 10-100)
    LineEndStyle m_arrowStyle;
    LineStyle m_lineStyle = LineStyle::Solid;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // Mosaic cursor cache (avoid recreating on every mouse move)
    QCursor m_mosaicCursorCache;
    int m_mosaicCursorCacheWidth = -1;

    // Shape tool state
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;

    // In-progress annotation state (managed by ToolManager, m_isDrawing tracks overall state)
    bool m_isDrawing;

    // Selection state flags
    bool m_isClosing;
    bool m_isDialogOpen;  // Prevents close during file dialog

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

    // Text annotation editor component (handles editing, transformation, formatting, dragging)
    TextAnnotationEditor *m_textAnnotationEditor;

    // Color picker dialog
    ColorPickerDialog *m_colorPickerDialog;

    // Startup protection - ignore early ApplicationDeactivate events
    QElapsedTimer m_createdAt;

    // Magnifier panel component
    MagnifierPanel* m_magnifierPanel;

    // Update throttling component
    UpdateThrottler m_updateThrottler;

    // Dirty region tracking for partial updates
    QRect m_lastSelectionRect;  // Previous selection rect for dirty region calculation
    QRect m_lastMagnifierRect;  // Previous magnifier rect
};

#endif // REGIONSELECTOR_H
