#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>
#include <memory>
#include <optional>

#include "AnnotationLayer.h"
#include "ToolbarWidget.h"
#include "InlineTextEditor.h"
#include "WindowDetector.h"

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
    Text,
    Mosaic,
    StepBadge,
    Eraser,
    Undo,
    Redo,
    Cancel,
    OCR,
    Pin,
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

    void setWindowDetector(WindowDetector *detector);

signals:
    void regionSelected(const QPixmap &screenshot, const QPoint &globalPosition);
    void selectionCancelled();
    void saveRequested(const QPixmap &screenshot);
    void copyRequested(const QPixmap &screenshot);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
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

    // Selection resize/move helpers
    ResizeHandle getHandleAtPosition(const QPoint &pos);
    void updateResize(const QPoint &pos);
    void updateCursorForHandle(ResizeHandle handle);

    QPixmap m_backgroundPixmap;
    QImage m_backgroundImageCache;  // 快取的 QImage，避免重複轉換
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
    QVector<QPoint> m_currentPath;

    // Temporary annotation objects for preview
    std::unique_ptr<PencilStroke> m_currentPencil;
    std::unique_ptr<MarkerStroke> m_currentMarker;
    std::unique_ptr<ArrowAnnotation> m_currentArrow;
    std::unique_ptr<RectangleAnnotation> m_currentRectangle;
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

    // OCR state
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;

    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);

    // Inline text editing
    InlineTextEditor *m_textEditor;

    // Color picker dialog
    ColorPickerDialog *m_colorPickerDialog;
};

#endif // REGIONSELECTOR_H
