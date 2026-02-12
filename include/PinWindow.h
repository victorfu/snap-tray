#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>
#include <cstddef>
#include <memory>


// Shared pixmap type for explicit memory sharing across mosaic annotations
using SharedPixmap = std::shared_ptr<const QPixmap>;
#include "WatermarkRenderer.h"
#include "LoadingSpinnerRenderer.h"
#include "pinwindow/ResizeHandler.h"
#include "tools/ToolId.h"
#include "annotation/AnnotationHostAdapter.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "TransformationGizmo.h"

class QMenu;
class QLabel;
class QTimer;
class QFutureWatcherBase;
class QScreen;
class QPainter;
class ICaptureEngine;
class OCRManager;
struct OCRResult;
class QRCodeManager;
class PinWindowManager;
class UIIndicators;
class WindowedToolbar;
class WindowedSubToolbar;
class AnnotationLayer;
class AnnotationItem;
class ToolManager;
class ToolOptionsPanel;
class InlineTextEditor;
class TextAnnotationEditor;
class AutoBlurManager;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;
class RegionLayoutManager;
class RegionSettingsHelper;
struct LayoutRegion;
class AnnotationContext;

namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}

class PinWindow : public QWidget, public AnnotationHostAdapter
{
    Q_OBJECT

public:
    explicit PinWindow(const QPixmap& screenshot,
                       const QPoint& position,
                       QWidget* parent = nullptr,
                       bool autoSaveToCache = true);
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
    void setWatermarkSettings(const WatermarkRenderer::Settings& settings);
    WatermarkRenderer::Settings watermarkSettings() const { return m_watermarkSettings; }

    // Pin window manager
    void setPinWindowManager(PinWindowManager* manager);

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

    // Live capture mode
    void setSourceRegion(const QRect& region, QScreen* screen);
    void startLiveCapture();
    void stopLiveCapture();
    void pauseLiveCapture();
    void resumeLiveCapture();
    bool isLiveMode() const { return m_isLiveMode; }
    bool isLivePaused() const { return m_livePaused; }
    void setLiveFrameRate(int fps);

    // OCR language settings
    void updateOcrLanguages(const QStringList& languages);

    // Region Layout Mode (for multi-region captures)
    void setMultiRegionData(const QVector<LayoutRegion>& regions);
    bool hasMultiRegionData() const { return m_hasMultiRegionData; }
    void enterRegionLayoutMode();
    void exitRegionLayoutMode(bool apply);
    bool isRegionLayoutMode() const;
    void prepareForMerge();
    QPixmap exportPixmapForMerge() const;

signals:
    void closed(PinWindow* window);
    void saveRequested(const QPixmap& pixmap);
    void saveCompleted(const QPixmap& pixmap, const QString& filePath);
    void saveFailed(const QString& filePath, const QString& error);
    void ocrCompleted(bool success, const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    // AnnotationHostAdapter implementation
    QWidget* annotationHostWidget() const override;
    AnnotationLayer* annotationLayerForContext() const override;
    ToolOptionsPanel* toolOptionsPanelForContext() const override;
    InlineTextEditor* inlineTextEditorForContext() const override;
    TextAnnotationEditor* textAnnotationEditorForContext() const override;
    void onContextColorSelected(const QColor& color) override;
    void onContextMoreColorsRequested() override;
    void onContextLineWidthChanged(int width) override;
    void onContextArrowStyleChanged(LineEndStyle style) override;
    void onContextLineStyleChanged(LineStyle style) override;
    void onContextFontSizeDropdownRequested(const QPoint& pos) override;
    void onContextFontFamilyDropdownRequested(const QPoint& pos) override;
    void onContextTextEditingFinished(const QString& text, const QPoint& position) override;
    void onContextTextEditingCancelled() override;


    void updateSize();
    void createContextMenu();
    void mergePinsFromContextMenu();
    int eligibleMergePinCount() const;
    void adjustOpacityByStep(int direction);
    void saveToFile();
    void copyToClipboard();
    QPixmap getTransformedPixmap() const;
    QPixmap getExportPixmapCore(bool includeDisplayEffects) const;
    void drawAnnotationsForExport(QPainter& painter, const QSize& logicalSize) const;
    QPixmap getExportPixmap() const;

    // Performance optimization: ensure transform cache is valid
    void ensureTransformCacheValid() const;
    void onResizeFinished();

    // Rounded corner handling
    int effectiveCornerRadius(const QSize& contentSize) const;

    // Click-through handling
    void applyClickThroughState(bool enabled);
    void updateClickThroughForCursor();

    // OCR methods
    void performOCR();
    void onOCRComplete(const OCRResult& result);
    void showOCRResultDialog(const OCRResult& result);

    // QR Code methods
    void performQRCodeScan();
    void onQRCodeComplete(bool success, const QString& text, const QString& format, const QString& error);

    // Info methods
    void copyAllInfo();

    // Cache folder methods
    static QString cacheFolderPath();
    void openCacheFolder();
    void saveToCacheAsync();

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
    void onColorSelected(const QColor& color);
    void onWidthChanged(int width);
    void onEmojiSelected(const QString& emoji);
    void onStepBadgeSizeChanged(StepBadgeSize size);
    void onShapeTypeChanged(ShapeType type);
    void onShapeFillModeChanged(ShapeFillMode mode);
    void onArrowStyleChanged(LineEndStyle style);
    void onLineStyleChanged(LineStyle style);
    void onFontSizeDropdownRequested(const QPoint& pos);
    void onFontFamilyDropdownRequested(const QPoint& pos);
    void onFontSizeSelected(int size);
    void onFontFamilySelected(const QString& family);
    void onAutoBlurRequested();
    void onMoreColorsRequested();

    // Text annotation helper methods
    bool handleTextEditorPress(const QPoint& pos);
    bool handleTextAnnotationPress(const QPoint& pos);
    bool handleGizmoPress(const QPoint& pos);
    TextBoxAnnotation* getSelectedTextAnnotation();

    // Arrow and Polyline editing state
    bool m_isArrowDragging = false;
    GizmoHandle m_arrowDragHandle = GizmoHandle::None;
    bool m_isPolylineDragging = false;
    int m_activePolylineVertexIndex = -1;
    // Note: m_dragStartPos is used for window dragging (global coords)
    QPoint m_annotationDragStartPos; // For annotation dragging (original coords)

    // Arrow and Polyline helpers
    bool handleArrowAnnotationPress(const QPoint& pos);
    bool handleArrowAnnotationMove(const QPoint& pos);
    bool handleArrowAnnotationRelease(const QPoint& pos);
    ArrowAnnotation* getSelectedArrowAnnotation();

    bool handlePolylineAnnotationPress(const QPoint& pos);
    bool handlePolylineAnnotationMove(const QPoint& pos);
    bool handlePolylineAnnotationRelease(const QPoint& pos);
    PolylineAnnotation* getSelectedPolylineAnnotation();

    // Cursor update helper
    void updateAnnotationCursor(const QPoint& pos);

    // Coordinate transformation for rotated/flipped state
    QPoint mapToOriginalCoords(const QPoint& displayPos) const;
    QPoint mapFromOriginalCoords(const QPoint& originalPos) const;
    QPointF mapToOriginalCoords(const QPointF& displayPos) const;
    QPointF mapFromOriginalCoords(const QPointF& originalPos) const;

    // Keep text result aligned with inline editor orientation at creation time.
    void applyTextOrientationCompensation(TextBoxAnnotation* textItem,
                                          const QPointF& baselineOriginal) const;
    void applyEmojiOrientationCompensation(EmojiStickerAnnotation* emojiItem) const;
    void compensateNewestEmojiIfNeeded(const AnnotationItem* previousLastItem) const;
    void applyStepBadgeOrientationCompensation(StepBadgeAnnotation* badgeItem) const;
    void compensateNewestStepBadgeIfNeeded(const AnnotationItem* previousLastItem) const;

    // Original members
    QPixmap m_originalPixmap;
    SharedPixmap m_sharedSourcePixmap;  // Shared for mosaic tool memory efficiency
    QPixmap m_displayPixmap;
    qreal m_zoomLevel;
    QPoint m_dragStartPos;
    bool m_isDragging;
    QMenu* m_contextMenu;
    QAction* m_showToolbarAction = nullptr;
    QAction* m_clickThroughAction = nullptr;
    QAction* m_showBorderAction = nullptr;
    QAction* m_adjustRegionLayoutAction = nullptr;
    QAction* m_mergePinsAction = nullptr;

    // Live capture context menu items
    QAction* m_startLiveAction = nullptr;
    QAction* m_pauseLiveAction = nullptr;
    QMenu* m_fpsMenu = nullptr;

    // Zoom menu members
    QAction* m_currentZoomAction;
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
    QTimer* m_clickThroughHoverTimer = nullptr;

    // Border visibility
    bool m_showBorder = true;

    // Rotation members
    int m_rotationAngle;  // 0, 90, 180, 270 degrees

    // Flip members
    bool m_flipHorizontal;
    bool m_flipVertical;

    // OCR members
    OCRManager* m_ocrManager;
    bool m_ocrInProgress;
    LoadingSpinnerRenderer* m_loadingSpinner;

    // QR Code members
    QRCodeManager* m_qrCodeManager;
    bool m_qrCodeInProgress;

    // Watermark members
    WatermarkRenderer::Settings m_watermarkSettings;

    // Pin window manager
    PinWindowManager* m_pinWindowManager = nullptr;

    // Performance optimization: transform cache
    mutable QPixmap m_transformedCache;
    mutable int m_cachedRotation = -1;
    mutable bool m_cachedFlipH = false;
    mutable bool m_cachedFlipV = false;

    // Resize optimization
    QTimer* m_resizeFinishTimer = nullptr;
    bool m_pendingHighQualityUpdate = false;

    int m_baseCornerRadius = 0;

    // Toolbar and annotation members
    WindowedToolbar* m_toolbar = nullptr;
    WindowedSubToolbar* m_subToolbar = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    InlineTextEditor* m_textEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
    RegionSettingsHelper* m_settingsHelper = nullptr;
    bool m_toolbarVisible = false;
    bool m_annotationMode = false;
    ToolId m_currentToolId = ToolId::Selection;
    QColor m_annotationColor;
    int m_annotationWidth = 3;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // AutoBlur
    AutoBlurManager* m_autoBlurManager = nullptr;
    bool m_autoBlurInProgress = false;
    QPointer<QFutureWatcherBase> m_autoBlurWatcher;

    // Color picker dialog
    snaptray::colorwidgets::ColorPickerDialogCompat* m_colorPickerDialog = nullptr;

    // Shared annotation setup/signals helper
    std::unique_ptr<AnnotationContext> m_annotationContext;

    // Region Layout Mode
    RegionLayoutManager* m_regionLayoutManager = nullptr;
    QVector<LayoutRegion> m_storedRegions;
    bool m_hasMultiRegionData = false;
    QPoint m_layoutModeMousePos;  // For hover effects in layout mode

    // Live capture mode
    QRect m_sourceRegion;
    QPointer<QScreen> m_sourceScreen;
    bool m_isDestructing = false;
    bool m_isLiveMode = false;
    ICaptureEngine* m_captureEngine = nullptr;
    QTimer* m_captureTimer = nullptr;
    QTimer* m_liveIndicatorTimer = nullptr;
    int m_captureFrameRate = kLivePreviewFps;
    bool m_livePaused = false;

    // ========================================================================
    // Local constants (previously in Constants.h but only used by PinWindow)
    // ========================================================================

    // Frame rate for live preview capture
    static constexpr int kLivePreviewFps = 15;

    // Pin window UI dimensions
    static constexpr int kMinPinSize = 50;
    static constexpr int kLiveIndicatorRadius = 6;
    static constexpr int kLiveIndicatorMargin = 8;

    // Live indicator animation parameters
    static constexpr double kPulseBase = 0.5;
    static constexpr double kPulseAmplitude = 0.5;
    static constexpr double kPulsePeriodMs = 500.0;
    static constexpr int kMinAlpha = 150;
    static constexpr int kAlphaRange = 100;
    static constexpr int kPausedDarkerPercent = 80;

    // Misc PinWindow-specific constants
    static constexpr int kFullOpacity = 255;
    static constexpr int kMaxFileCollisionRetries = 100;
    static constexpr int kMosaicBlockSize = 12;

    void updateLiveFrame();
};

#endif // PINWINDOW_H
