#ifndef PINWINDOW_H
#define PINWINDOW_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>
#include <QHash>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>


// Shared pixmap type for explicit memory sharing across mosaic annotations
using SharedPixmap = std::shared_ptr<const QPixmap>;
#include "beautify/BeautifySettings.h"
#include "WatermarkRenderer.h"
#include "LoadingSpinnerRenderer.h"
#include "pinwindow/ResizeHandler.h"
#include "pinwindow/PinLayoutGeometry.h"
#include "pinwindow/ForegroundCutout.h"
#include "tools/ToolId.h"
#include "annotation/AnnotationHostAdapter.h"
#include "region/ShapeAnnotationEditor.h"
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
class PinHoverBar;
class PinHoverBarViewModel;
namespace SnapTray {
class QmlToast;
class QmlWindowedToolbar;
class QmlFloatingSubToolbar;
class QmlEmojiPickerPopup;
class QmlBeautifyPanel;
class PinExportRunner;
class ForegroundCutoutRunner;
struct PinExportRequest;
class QmlDialog;
class ImageExportDialogController;
struct CaptureSessionWriteRequest;
class ToolbarTooltipController;
}
class PinToolbarViewModel;
class PinToolOptionsViewModel;
class AnnotationStyleEditor;
class ColorPickOverlay;
struct ColorPickSource;
class AnnotationLayer;
class AnnotationItem;
class ToolManager;
class CropToolHandler;
class CutOutToolHandler;
namespace ImageStrip { struct Request; }
class InlineTextEditor;
class TextAnnotationEditor;
class AutoBlurManager;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ShapeAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;
class RegionLayoutManager;
class RegionSettingsHelper;
struct LayoutRegion;
class AnnotationContext;
class TestPinWindowCropUndo;
class TestPinWindowStripCutOut;

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
                       bool persistHistorySnapshot = false,
                       bool showImmediately = true);
    ~PinWindow();
    void showPreparedWindow();

    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return m_zoomLevel; }

    void setOpacity(qreal opacity);
    qreal opacity() const { return m_opacity; }
    // User-facing value changes (#225): apply and show the named badge.
    void resetZoom();
    void resetOpacity();
    void zoomByStep(int direction);

    void rotateRight();  // Rotate clockwise by 90 degrees
    void rotateLeft();   // Rotate counter-clockwise by 90 degrees
    void flipHorizontal();  // Flip horizontally (mirror left-right)
    void flipVertical();    // Flip vertically (mirror top-bottom)

    // Watermark settings
    void setWatermarkSettings(const WatermarkRenderer::Settings& settings);
    WatermarkRenderer::Settings watermarkSettings() const { return m_watermarkSettings; }

    // Pin window manager
    void setPinWindowManager(PinWindowManager* manager);
    // Finishes a save or Beautify export still being written, without
    // reporting it. Called when the app quits, where closing a pin no longer
    // deletes it, so the file is never left half-written (#353).
    void finishExportBeforeQuit();

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
    // The history entry this pin's picture came from, and the Beautify
    // settings to continue from when that entry was itself beautified (#344).
    void setHistoryOrigin(const QString& entryId, const std::optional<BeautifySettings>& beautify);
    void startLiveCapture();
    void stopLiveCapture();
    QList<WId> liveCaptureExcludedWindowIds();
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
    PinLayoutGeometry::Source mergeSourceGeometry() const;
    QPixmap exportPixmapForMerge() const;
    bool isAutoBlurInProgress() const { return m_autoBlurInProgress; }

signals:
    void closed(PinWindow* window);
    void saveRequested(const QPixmap& pixmap);
    void saveCompleted(const QPixmap& pixmap, const QString& filePath);
    void saveFailed(const QString& filePath, const QString& error);
    void ocrCompleted(bool success, const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    // The Live dot's hint (#225), only over the dot; empty elsewhere.
    QString liveToolTipAt(const QPoint& pos) const;
    void focusInEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    friend class TestPinWindowCropUndo;
    friend class TestPinWindowStripCutOut;
    friend class TestPinWindowRemoveBackground;
    friend class TestPinMergeHelper;
    friend class TestPinWindowStyleSync;
    friend class TestPinWindowTransform;
    friend class TestOCRBusyRepaint;
    friend class TestPinHoverIntegration;
    friend class TestPinContextMenuLayout;
    friend class TestPinWindowEvidence;
    friend class TestPinWindowBeautifyHistory;
    friend class TestPinWindowTextToolFormatting;
    friend class TestEraserHostRouting;
    struct CropUndoEntry;

    // AnnotationHostAdapter implementation
    QWidget* annotationHostWidget() const override;
    AnnotationLayer* annotationLayerForContext() const override;
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


    void updateSize(bool liveFrame = false);
    void createContextMenu();
    void refreshMoveToScreenMenu();
    void moveToScreen(QScreen* targetScreen);
    void handleApplicationStateChanged(Qt::ApplicationState state);
    QString screenMenuLabel(QScreen* screen, int index, bool isPrimary) const;
    void mergePinsFromContextMenu();
    void showRegionLayoutDialog();
    int eligibleMergePinCount() const;
    void adjustOpacityByStep(int direction);
    void saveToFile();
    void copyToClipboard();
    bool ensureAutoBlurReadyForExport();
    // Remove Background (#363): segments the base image off the GUI thread
    // and replaces it with the cut-out as one crop-undo step. Annotations
    // stay editable on top; mosaics resample the new image.
    void removeBackground();
    // Any task the loading spinner stands for; one place for its timer and
    // its painting, so the two cannot disagree.
    bool hasSpinnerTask() const;
    // Esc while it runs: drops the job, the pin stays as it was.
    bool cancelBackgroundRemoval();
    void onBackgroundRemovalFinished(const SnapTray::ForegroundCutout::Result& result,
                                     qint64 sourceCacheKey);
    // Why the image cannot be replaced right now, or an empty string.
    QString backgroundRemovalBlocker() const;
    bool applyForegroundCutout(const QImage& image, const QImage& mask);
    // While an earlier save, Beautify save or Beautify copy of this pin is
    // still running, tells the user and returns true; otherwise false.
    bool rejectIfExportInProgress();
    // Composes (beautified when request.beautify is set) and writes the
    // request's snapshot off the GUI thread, then reports it with
    // saveCompleted/saveFailed once the file is published or has failed.
    // `snapshot` is the pixmap saveCompleted reports for an unbeautified
    // save. Edits made meanwhile are not part of the file (#353).
    void startFileExport(SnapTray::PinExportRequest request, const QPixmap& snapshot,
                         bool rememberDirectoryOnSuccess,
                         std::optional<SnapTray::CaptureSessionWriteRequest> history);
    QPixmap getExportPixmapCore(bool includeDisplayEffects, bool nativeResolution = false) const;
    void drawAnnotationsForExport(QPainter& painter, const QSize& logicalSize,
                                  bool nativeResolution = false) const;
    QPixmap getExportPixmap() const;

    // Performance optimization: ensure transform cache is valid
    void ensureTransformCacheValid() const;
    void onResizeFinished();
    void applyPendingDragPosition();
    void endWindowDrag();

    // Rounded corner handling
    int effectiveCornerRadius(const QSize& contentSize) const;

    // Click-through handling
    void applyClickThroughState(bool enabled);
    void updateClickThroughForCursor();

    // OCR methods
    void updateToolbarAutoBlurState();
    void invalidateAutoBlurRequest();
    bool isAutoBlurRequestCurrent(quint64 generation) const;
    static QPixmap buildAutoBlurAnnotationSource(const QPixmap& displayPixmap,
                                                 int rotationAngle,
                                                 bool flipHorizontal,
                                                 bool flipVertical);
    static QRect mapAutoBlurDetectionRect(const QRect& physicalRect,
                                          qreal devicePixelRatio,
                                          const QSize& displayLogicalSize,
                                          const QSize& annotationLogicalSize,
                                          int rotationAngle,
                                          bool flipHorizontal,
                                          bool flipVertical);
    OCRManager* ensureOCRManager();
    void updateLoadingSpinnerState();
    void performOCR();
    void onOCRComplete(const OCRResult& result, const QPixmap& source = QPixmap());
    void showOCRResultDialog(const OCRResult& result, const QPixmap& source = QPixmap());

    // QR Code methods
    void performQRCodeScan();
    void onQRCodeComplete(bool success, const QString& text, const QString& format, const QString& error);

    // Info methods
    void copyAllInfo();
    void refreshInfoMenu();
    QString currentDisplaySizeText() const;
    QSize baseContentLogicalSize() const;
    QSize transformedContentLogicalSize() const;
    void setContentLogicalSize(const QSize& logicalSize);
    void updateAccessibleName();
    static QRectF preciseSourceSampleRectForRegion(const QRect& globalRegion,
                                                   const QRect& screenGeometry,
                                                   qreal dpr);
    QRectF sourceSampleRect() const;
    QRectF transformedSourceSampleRect() const;
    void resetSourceSampleRect();
    void setSourceSampleRect(const QRectF& sampleRect);
    static QRectF displaySourceRectForTarget(const QRectF& sampleRect,
                                             const QSize& sourcePixelSize,
                                             const QSize& targetPixelSize);
    QPixmap buildDisplayPixmap(const QSize& logicalSize, Qt::TransformationMode mode) const;

    // Cache folder methods
    static QString cacheFolderPath();
    void openCacheFolder();

    // Toolbar and annotation methods
    void showToolbar();
    void hideToolbar();
    void hideToolbarPreservingToolState();
    void initializeAnnotationComponents();
    void refreshMosaicSources(bool liveFrame = false);
    void dismissBeautifyPanelIfVisible();
    void clearSelectedToolForBeautify();
    void syncToolbarActiveButtonForVisibleState();
    void updateToolbarPosition();
    void enterAnnotationMode();
    void exitAnnotationMode(bool clearActiveTool = true);
    void updateCursorForTool();
    void handleToolbarToolSelected(int toolId);
    void handleToolbarUndo();
    void handleToolbarRedo();
    QSize cropToolImageSize() const;
    void syncCropHandlerImageSize();
    void updateUndoRedoState();
    bool isAnnotationTool(ToolId toolId) const;
    QPixmap getExportPixmapWithAnnotations() const;
    QSize regionLayoutContentViewSize(const QSize& layoutSize) const;
    QSize regionLayoutViewportSize(const QSize& layoutSize) const;
    QRect regionLayoutControlsRect(const QSize& layoutSize) const;
    QTransform regionLayoutViewTransform(const QSize& layoutSize) const;
    QPoint regionLayoutPositionFromWidget(const QPointF& widgetPos) const;
    ResizeHandler::Edge regionLayoutHandleAtWidget(const QPoint& widgetPos) const;
    void keepRegionLayoutControlsOnScreen();
    void drawRegionLayoutAnnotations(QPainter& painter,
                                     const QSize& layoutSize,
                                     bool normalizeToRecomposedOrigin,
                                     bool nativeResolution = false) const;
    void updateSubToolbarPosition();
    void hideSubToolbar();
    void showEmojiPickerPopup();
    bool isGlobalPosOverFloatingUi(const QPoint& globalPos) const;
    void rebuildManagedCursorAt(const QPoint& localPos);
    void restoreAnnotationCursorAt(const QPoint& localPos);
    void syncFloatingUiCursor();
    // Crops to a rect in tool (view) coordinates, widened to whole logical
    // pixels. The crop tool commits through applyCropSourcePixels() instead;
    // both end in commitCropGeometry().
    void applyCrop(const QRect& cropRect);
    // Crops to a rect in source pixels (the crop draft's units, #358).
    // Returns false, leaving the image and annotations as they are, when the
    // rect cannot be applied (an annotation drag in progress is cancelled
    // either way). A rect covering the whole image returns true: it leaves
    // the crop tool without changing the image or adding an undo entry.
    bool applyCropSourcePixels(const QRect& sourcePixelRect);
    // Enter, Apply and double-click: applies the crop draft, or tells the
    // user that it could not be applied.
    bool commitCropDraft();
    // Abandons a crop drag whose release may never come.
    void cancelImageEditGesture();
    // Any annotation, shown or kept in undo/redo history.
    bool hasOwnedAnnotations() const;
    // Grid for the crop draft's corner; see CropDraftModel::setPositionStep().
    QSize cropPositionStep() const;
    bool commitCropGeometry(const QRectF& exactPhysicalCropRect,
                            const QSize& croppedLogicalSize,
                            const QPointF& annotationOffsetDisplay);
    void leaveCropTool();
    QSize cropSourcePixelSize() const;
    CropToolHandler* cropToolHandler() const;
    bool isCropToolActive() const;
    void syncCropDraftUi();
    bool handleCropDraftKey(QKeyEvent* event);
    void refreshCropCursor();
    // Cut-out tool (#371): removes a band across the image or inserts a blank
    // one, as one undoable image change. Returns false, changing nothing,
    // when the strip cannot be applied.
    bool applyStrip(const ImageStrip::Request& request);
    CutOutToolHandler* cutOutToolHandler() const;
    bool isCutOutToolActive() const;
    // Crop or cut-out: tools that edit the image and own every pointer press.
    bool isImageEditToolActive() const;
    void syncCutOutDraftUi();
    // Enter, Apply and double-click for the cut-out band.
    bool commitCutOutDraft();
    bool handleCutOutDraftKey(QKeyEvent* event);
    // An arrow key as a step in unrotated source pixels (1, or 10 with Shift).
    // False for any other key or modifier.
    bool arrowKeySourceStep(const QKeyEvent* event, QPoint* sourceStep) const;
    void undoCrop();
    void redoCrop();
    bool canUndoCrop();
    bool canRedoCrop();
    void clearCropUndoHistory();
    bool matchesCropAnnotationBoundary(const CropUndoEntry& entry) const;
    void pruneInvalidCropHistory();
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
    // Eyedropper (#271): picks from the pinned image, not its annotations.
    void startColorPick();
    ColorPickSource colorPickSource() const;
    void applyPickedColor(const QColor& color);

    // Text annotation helper methods
    bool handleTextEditorPress(const QPoint& pos);
    bool handleTextAnnotationPress(const QPoint& pos);
    bool handleGizmoPress(const QPoint& pos);
    TextBoxAnnotation* getSelectedTextAnnotation();
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation();
    ShapeAnnotation* getSelectedShapeAnnotation();

    bool handleEmojiStickerAnnotationPress(const QPoint& pos);
    bool handleEmojiStickerAnnotationMove(const QPoint& pos);
    bool handleEmojiStickerAnnotationRelease(const QPoint& pos);

    bool m_isEmojiDragging = false;
    bool m_isEmojiScaling = false;
    bool m_isEmojiRotating = false;
    GizmoHandle m_emojiDragHandle = GizmoHandle::None;
    QPoint m_emojiStartPos;
    qreal m_emojiStartScale = 1.0;
    qreal m_emojiStartDistance = 0.0;
    QPointF m_emojiStartCenter;
    qreal m_emojiStartRotation = 0.0;
    qreal m_emojiStartAngle = 0.0;

    // Arrow and Polyline editing state
    bool m_isArrowDragging = false;
    GizmoHandle m_arrowDragHandle = GizmoHandle::None;
    bool m_isPolylineDragging = false;
    bool m_undoStateShowsGesture = false;   // #237: toolbar Undo/Redo reflect a drag
    int m_activePolylineVertexIndex = -1;
    // Note: m_dragStartPos is used for window dragging (global coords)
    QPoint m_annotationDragStartPos; // For annotation dragging (original coords)
    bool m_consumeNextToolRelease = false;

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
    bool hasActiveAnnotationInteraction() const;
    // Escape during a drag of an existing annotation (#237): puts it back
    // without an undo step. Returns true if one was in progress.
    bool cancelAnnotationGesture();
    QRect mappedAnnotationRect(const QRect& originalRect) const;
    QRect selectedAnnotationInteractionVisualRect() const;
    bool requestLocalizedToolRepaint();
    void resetAnnotationInteractionTracking();

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
    bool m_mosaicSourceDirty = false;
    bool m_hasVisibleMosaicAnnotations = false;
    QPixmap m_displayPixmap;
    QSize m_contentLogicalSize;
    QRectF m_sourceSampleRect;
    qreal m_zoomLevel;
    QRect m_lastAnnotationInteractionVisualRect;
    QPoint m_dragStartPos;
    QPoint m_initialPosition;
    bool m_hasPerformedInitialShow = false;
    bool m_isDragging;
    QTimer* m_dragMoveTimer = nullptr;
    QPoint m_pendingDragPosition;
    bool m_hasPendingDragPosition = false;
    QMenu* m_contextMenu;
    QMenu* m_moveToScreenMenu = nullptr;
    QMenu* m_infoMenu = nullptr;
    QMenu* m_arrangeMenu = nullptr;   // drawing order of the selected annotation (#268)
    QAction* m_showToolbarAction = nullptr;
    QAction* m_clickThroughAction = nullptr;
    QAction* m_showBorderAction = nullptr;
    QAction* m_adjustRegionLayoutAction = nullptr;
    QAction* m_mergePinsAction = nullptr;
    QAction* m_sizeInfoAction = nullptr;
    QAction* m_zoomInfoAction = nullptr;
    QAction* m_rotationInfoAction = nullptr;
    QAction* m_opacityInfoAction = nullptr;
    QAction* m_flipHorizontalInfoAction = nullptr;
    QAction* m_flipVerticalInfoAction = nullptr;

    // Live capture context menu items
    QAction* m_startLiveAction = nullptr;
    QAction* m_pauseLiveAction = nullptr;
    QMenu* m_fpsMenu = nullptr;

    // Zoom menu members
    QAction* m_currentZoomAction;
    QAction* m_currentOpacityAction = nullptr;
    bool m_smoothing;

    // Components
    ResizeHandler* m_resizeHandler;
    UIIndicators* m_uiIndicators;
    // The hover bar (#225), created on first hover or focus.
    PinHoverBarViewModel* m_hoverBarViewModel = nullptr;
    PinHoverBar* m_hoverBar = nullptr;
    SnapTray::ToolbarTooltipController* m_liveTooltip = nullptr;
    void ensureHoverBar();
    void ensureLiveTooltip();
    void syncHoverBar();
    bool hoverBarSuppressed() const;
    void applyUserZoom(qreal zoom);
    void applyUserOpacity(qreal opacity);
    void showZoomBadge();
    void showOpacityBadge();
    void refreshContextMenuState();
    SnapTray::QmlToast* m_toast;

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
    std::unique_ptr<SnapTray::QmlWindowedToolbar> m_toolbar;
    std::unique_ptr<SnapTray::QmlFloatingSubToolbar> m_subToolbar;
    AnnotationLayer* m_annotationLayer = nullptr; // Qt parent owns lifetime
    ToolManager* m_toolManager = nullptr;
    InlineTextEditor* m_textEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
    std::unique_ptr<ShapeAnnotationEditor> m_shapeAnnotationEditor;
    SnapTray::QmlEmojiPickerPopup* m_emojiPickerPopup = nullptr;
    RegionSettingsHelper* m_settingsHelper = nullptr;
    bool m_toolbarVisible = false;
    bool m_annotationMode = false;
    AnnotationStyleEditor* m_styleEditor = nullptr;   // Qt parent owns lifetime
    struct CropUndoEntry {
        QPixmap pixmap;
        QSize contentLogicalSize;
        QRectF sourceSampleRect;
        int rotationAngle;
        bool flipH;
        bool flipV;
        QVector<LayoutRegion> storedRegions;
        bool hasMultiRegionData = false;
        QPixmap croppedPixmap;
        QSize croppedContentLogicalSize;
        QRectF croppedSourceSampleRect;
        int croppedRotationAngle = 0;
        bool croppedFlipH = false;
        bool croppedFlipV = false;
        QVector<LayoutRegion> croppedStoredRegions;
        bool croppedHasMultiRegionData = false;
        QPointF annotationOffsetDisplay;
        std::uint64_t annotationHistoryState = 0;
        bool isLayout = false;
        // A strip cut-out: per-item shifts are kept in layoutAnnotationOffsets.
        bool isStrip = false;
        qreal layoutViewScale = 1.0;
        std::uint64_t layoutHistoryCeiling = 0;
        QHash<QPair<quint64, quint64>, QPointF> layoutAnnotationOffsets;
    };
    QVector<CropUndoEntry> m_cropUndoStack;
    QVector<CropUndoEntry> m_cropRedoStack;
    static constexpr int kMaxCropUndoSize = 10;
    // Arrow keys move the crop draft by this many source pixels (#358).
    static constexpr int kCropNudgeStep = 1;
    static constexpr int kCropNudgeLargeStep = 10;   // with Shift
    ToolId m_currentToolId = ToolId::Selection;
    QColor m_annotationColor;
    int m_annotationWidth = 3;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // AutoBlur
    AutoBlurManager* m_autoBlurManager = nullptr;
    bool m_autoBlurInProgress = false;
    QPointer<QFutureWatcherBase> m_autoBlurWatcher;
    quint64 m_autoBlurContentGeneration = 0;

    // Color picker dialog
    std::unique_ptr<snaptray::colorwidgets::ColorPickerDialogCompat> m_colorPickerDialog;
    QPointer<ColorPickOverlay> m_colorPickOverlay;

    // Shared annotation setup/signals helper
    std::unique_ptr<AnnotationContext> m_annotationContext;

    // Region Layout Mode
    RegionLayoutManager* m_regionLayoutManager = nullptr;
    QPointer<SnapTray::QmlDialog> m_regionLayoutDialog;
    SnapTray::ImageExportDialogController* m_imageExport = nullptr;   // child of this
    QVector<LayoutRegion> m_storedRegions;
    bool m_hasMultiRegionData = false;
    QPoint m_layoutModeMousePos;  // Widget/view coordinates for layout chrome hover
    QPoint m_regionLayoutOriginalWindowPos;
    bool m_hasRegionLayoutOriginalWindowPos = false;

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

    // Beautify
    void showBeautifyPanel();
    void onBeautifyCopy(const BeautifySettings& settings);
    void onBeautifySave(const BeautifySettings& settings);
    void onBeautifyExport(const BeautifySettings& settings);
    // Export dialog (#357): a smaller copy of the composed image. The pin,
    // its annotations and its history entry are left as they are.
    void exportImage();
    void openImageExport(const QPixmap& composed);
    SnapTray::ImageExportDialogController* imageExportController();
    // Image or annotations changed: what an open Export dialog would write is out of date.
    void closeStaleImageExport();
    // What the Beautify panel opens with: this pin's settings, else the
    // global last-used ones.
    BeautifySettings beautifyPanelSettings() const;
    // The new history entry for a beautified export of `source` with
    // `settings`. Built when the export's snapshot is taken so the document
    // matches it; the caller sets resultImage to the rendered picture and
    // submits it once the export has succeeded. Existing entries are never
    // rewritten.
    SnapTray::CaptureSessionWriteRequest beautifiedHistoryRequest(
        const QPixmap& source, const BeautifySettings& settings) const;
    // The pin's save, Beautify save and Beautify copy, one at a time (#353).
    std::unique_ptr<SnapTray::PinExportRunner> m_exportRunner;
    // Remove Background (#363); created on first use.
    std::unique_ptr<SnapTray::ForegroundCutoutRunner> m_cutoutRunner;
    SnapTray::ForegroundCutout::MaskProvider m_foregroundMaskProvider;
    bool m_backgroundRemovalInProgress = false;
    std::unique_ptr<SnapTray::QmlBeautifyPanel> m_beautifyPanel;
    // This pin's own Beautify settings once it has any; the panel opens with
    // them rather than with the global last-used ones.
    std::optional<BeautifySettings> m_beautifySettings;
    // History entry holding the editable document of this pin's picture, and
    // the picture that document produces. The entry is only reused while the
    // pin still exports exactly that picture.
    QString m_historyDocumentEntryId;
    QPixmap m_historyDocumentPixmap;

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
    void connectLiveCaptureEngineSignals();
};

#endif // PINWINDOW_H
