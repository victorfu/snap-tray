#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QElapsedTimer>
#include "WatermarkRenderer.h"
#include "LoadingSpinnerRenderer.h"
#include "pinwindow/ResizeHandler.h"
#include "tools/ToolId.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"

class QMenu;
class QLabel;
class QTimer;
class OCRManager;
class PinWindowManager;
class UIIndicators;
class PinWindowToolbar;
class PinWindowSubToolbar;
class AnnotationLayer;
class ToolManager;
class InlineTextEditor;
class TextAnnotationEditor;
class AutoBlurManager;
class TextAnnotation;

class PinWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PinWindow(const QPixmap &screenshot, const QPoint &position, QWidget *parent = nullptr);
    ~PinWindow();

    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return m_zoomLevel; }

    void setOpacity(qreal opacity);
    qreal opacity() const { return m_opacity; }

    void rotateRight();  // Rotate clockwise by 90 degrees
    void rotateLeft();   // Rotate counter-clockwise by 90 degrees
    void flipHorizontal();  // Flip horizontally (mirror left-right)
    void flipVertical();    // Flip vertically (mirror top-bottom)

    // Watermark settings
    void setWatermarkSettings(const WatermarkRenderer::Settings &settings);
    WatermarkRenderer::Settings watermarkSettings() const { return m_watermarkSettings; }

    // Pin window manager
    void setPinWindowManager(PinWindowManager *manager);

    // Click-through mode
    void setClickThrough(bool enabled);
    bool isClickThrough() const { return m_clickThrough; }

    // Border visibility
    void setShowBorder(bool enabled);
    bool showBorder() const { return m_showBorder; }

    // Toolbar visibility
    void toggleToolbar();
    bool isToolbarVisible() const { return m_toolbarVisible; }

    // Annotation mode
    bool isAnnotationMode() const { return m_annotationMode; }

signals:
    void closed(PinWindow *window);
    void saveRequested(const QPixmap &pixmap);
    void saveCompleted(const QPixmap &pixmap, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);
    void ocrCompleted(bool success, const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    // Layout constants
    static constexpr int kMinSize = 50;

    void updateSize();
    void createContextMenu();
    void saveToFile();
    void copyToClipboard();
    QPixmap getTransformedPixmap() const;
    QPixmap getExportPixmap() const;

    // Performance optimization: ensure transform cache is valid
    void ensureTransformCacheValid();
    void onResizeFinished();

    // Rounded corner handling
    int effectiveCornerRadius(const QSize &contentSize) const;

    // Click-through handling
    void applyClickThroughState(bool enabled);
    void updateClickThroughForCursor();

    // OCR methods
    void performOCR();
    void onOCRComplete(bool success, const QString &text, const QString &error);

    // Info methods
    void copyAllInfo();

    // Toolbar and annotation methods
    void showToolbar();
    void hideToolbar();
    void initializeAnnotationComponents();
    void updateToolbarPosition();
    void enterAnnotationMode();
    void exitAnnotationMode();
    void updateCursorForTool();
    void handleToolbarToolSelected(int toolId);
    void handleToolbarUndo();
    void handleToolbarRedo();
    void updateUndoRedoState();
    bool isAnnotationTool(ToolId toolId) const;
    QPixmap getExportPixmapWithAnnotations() const;
    void updateSubToolbarPosition();
    void hideSubToolbar();
    void onColorSelected(const QColor &color);
    void onWidthChanged(int width);
    void onEmojiSelected(const QString &emoji);
    void onStepBadgeSizeChanged(StepBadgeSize size);
    void onShapeTypeChanged(ShapeType type);
    void onShapeFillModeChanged(ShapeFillMode mode);
    void onArrowStyleChanged(LineEndStyle style);
    void onLineStyleChanged(LineStyle style);
    void onFontSizeDropdownRequested(const QPoint &pos);
    void onFontFamilyDropdownRequested(const QPoint &pos);
    void onAutoBlurRequested();

    // Text annotation helper methods
    bool handleTextEditorPress(const QPoint& pos);
    bool handleTextAnnotationPress(const QPoint& pos);
    bool handleGizmoPress(const QPoint& pos);
    TextAnnotation* getSelectedTextAnnotation();

    // Original members
    QPixmap m_originalPixmap;
    QPixmap m_displayPixmap;
    qreal m_zoomLevel;
    QPoint m_dragStartPos;
    bool m_isDragging;
    QMenu *m_contextMenu;
    QAction *m_showToolbarAction = nullptr;
    QAction *m_clickThroughAction = nullptr;
    QAction *m_showBorderAction = nullptr;

    // Zoom menu members
    QAction *m_currentZoomAction;
    bool m_smoothing;

    // Components
    ResizeHandler* m_resizeHandler;
    UIIndicators* m_uiIndicators;

    // Resize state
    bool m_isResizing;

    // Opacity members
    qreal m_opacity;

    // Click-through mode
    bool m_clickThrough;
    bool m_clickThroughApplied = false;
    QTimer *m_clickThroughHoverTimer = nullptr;

    // Border visibility
    bool m_showBorder = true;

    // Rotation members
    int m_rotationAngle;  // 0, 90, 180, 270 degrees

    // Flip members
    bool m_flipHorizontal;
    bool m_flipVertical;

    // OCR members
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
    LoadingSpinnerRenderer *m_loadingSpinner;

    // Watermark members
    WatermarkRenderer::Settings m_watermarkSettings;

    // Pin window manager
    PinWindowManager *m_pinWindowManager = nullptr;

    // Performance optimization: transform cache
    mutable QPixmap m_transformedCache;
    mutable int m_cachedRotation = -1;
    mutable bool m_cachedFlipH = false;
    mutable bool m_cachedFlipV = false;

    // Resize optimization
    QElapsedTimer m_resizeThrottleTimer;
    QTimer *m_resizeFinishTimer = nullptr;
    bool m_pendingHighQualityUpdate = false;
    static constexpr int kResizeThrottleMs = 16;  // ~60fps during resize

    int m_baseCornerRadius = 0;

    // Toolbar and annotation members
    PinWindowToolbar *m_toolbar = nullptr;
    PinWindowSubToolbar *m_subToolbar = nullptr;
    AnnotationLayer *m_annotationLayer = nullptr;
    ToolManager *m_toolManager = nullptr;
    InlineTextEditor *m_textEditor = nullptr;
    TextAnnotationEditor *m_textAnnotationEditor = nullptr;
    bool m_toolbarVisible = false;
    bool m_annotationMode = false;
    ToolId m_currentToolId = ToolId::Selection;
    QColor m_annotationColor;
    int m_annotationWidth = 3;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // AutoBlur
    AutoBlurManager *m_autoBlurManager = nullptr;
};

#endif // PINWINDOW_H
