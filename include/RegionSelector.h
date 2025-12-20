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

#ifdef Q_OS_MACOS
#include "WindowDetector.h"
#endif

class QScreen;
class ColorPaletteWidget;
class QCloseEvent;
class QTextEdit;

#ifdef Q_OS_MACOS
class OCRManager;
#endif
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
#ifdef Q_OS_MACOS
    OCR,
#endif
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

#ifdef Q_OS_MACOS
    // Window detection support
    void setWindowDetector(WindowDetector *detector);
#endif

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
    void drawToolbar(QPainter &painter);

    // Color palette helpers
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Window detection drawing
    void drawDetectedWindow(QPainter &painter);
    void drawWindowHint(QPainter &painter, const QString &title);
    void updateWindowDetection(const QPoint &localPos);

    void updateToolbarPosition();
    int getButtonAtPosition(const QPoint &pos);
    void handleToolbarClick(ToolbarButton button);
    QString getButtonTooltip(int buttonIndex);
    void drawTooltip(QPainter &painter);
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

    // Inline text editing
    void startInlineTextEdit(const QPoint &pos);
    void finishInlineTextEdit();
    void cancelInlineTextEdit();

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
    QRect m_toolbarRect;
    int m_hoveredButton;
    QVector<QRect> m_buttonRects;

    static const int TOOLBAR_HEIGHT = 40;
    static const int BUTTON_WIDTH = 36;
    static const int BUTTON_SPACING = 2;

    // Color palette
    ColorPaletteWidget *m_colorPalette;

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

#ifdef Q_OS_MACOS
    // Window detection state
    WindowDetector *m_windowDetector;
    std::optional<DetectedElement> m_detectedWindow;
    QRect m_highlightedWindowRect;
#endif

#ifdef Q_OS_MACOS
    // OCR state (macOS only)
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;

    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);
#endif

    // Icon rendering helpers
    void initializeIcons();
    QString getIconKeyForButton(ToolbarButton button) const;
    void renderIcon(QPainter &painter, const QRect &rect, ToolbarButton button, const QColor &color);

    // Inline text editing state
    QTextEdit *m_inlineTextEdit;
    bool m_isEditingText;
    QPoint m_textEditPosition;
};

#endif // REGIONSELECTOR_H
