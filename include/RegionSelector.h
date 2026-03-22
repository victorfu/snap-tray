#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include <QWidget>
#include <QImage>
#include <QMetaObject>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QElapsedTimer>
#include <QSettings>
#include <QPointer>
#include <memory>
#include <optional>
#include <functional>

#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/StepBadgeAnnotation.h"
#include "tools/ToolId.h"
#include "InlineTextEditor.h"
#include "WindowDetector.h"
#include "LoadingSpinnerRenderer.h"
#include "TransformationGizmo.h"
#include "TextFormattingState.h"
#include "annotation/AnnotationHostAdapter.h"
#include "history/HistoryStore.h"
#include "tools/ToolId.h"
#include "tools/ToolManager.h"
#include "region/SelectionStateManager.h"
#include "region/MagnifierPanel.h"
#include "region/UpdateThrottler.h"
#include "region/TextAnnotationEditor.h"
#include "region/ShapeAnnotationEditor.h"
#include "region/MultiRegionManager.h"
#include "region/RegionInputState.h"

class QScreen;
class RegionToolbarViewModel;
class PinToolOptionsViewModel;
namespace SnapTray {
class QmlFloatingToolbar;
class QmlFloatingSubToolbar;
class QmlOverlayPanel;
class QmlEmojiPickerPopup;
class QmlDialog;
}

namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}
class QCloseEvent;
class QResizeEvent;
class QFutureWatcherBase;
class OCRManager;
struct OCRResult;
class QRCodeManager;
class AutoBlurManager;
class QTimer;
class RegionPainter;
class RegionInputHandler;
class RegionToolbarHandler;
class RegionSettingsHelper;
class RegionExportManager;
class AnnotationSurfaceAdapter;
class MagnifierOverlay;
class AnnotationContext;
class CaptureShortcutHintsOverlay;
class RegionControlViewModel;
class MultiRegionListViewModel;
class ShareUploadClient;
namespace SnapTray { class QmlToast; }

// ShapeType and ShapeFillMode are defined in annotations/ShapeAnnotation.h

class RegionSelector : public QWidget, public AnnotationHostAdapter
{
    Q_OBJECT

    friend class tst_RegionSelectorMultiRegionSubToolbar;
    friend class tst_RegionSelectorHistoryReplay;
    friend class tst_RegionSelectorTransientUiCancelGuard;
    friend class TestRegionSelectorStyleSync;
    friend class tst_RegionSelectorDeferredInitialization;

public:
    explicit RegionSelector(QWidget *parent = nullptr);
    ~RegionSelector();

    // 初始化指定螢幕的截圖 (由 CaptureManager 調用)
    void initializeForScreen(QScreen *screen, const QPixmap &preCapture = QPixmap());

    // 初始化指定螢幕並使用預設區域 (用於錄影取消後返回)
    void initializeWithRegion(QScreen *screen, const QRect &region);

    // 設置 Quick Pin 模式 (選擇區域後直接 pin，不顯示 toolbar)
    void setQuickPinMode(bool enabled);

    // Control whether capture shortcut hints are shown on entry.
    void setShowShortcutHintsOnEntry(bool enabled);

    // Switch capture context to another screen while selector is active.
    void switchToScreen(QScreen *screen, bool preserveCompletedSelection = true);
    bool beginHistoryReplay(const QString& entryId);

    // Check if selection is complete (for external queries)
    bool isSelectionComplete() const;

    void setWindowDetector(WindowDetector *detector);
    void refreshWindowDetectionAtCursor();

    // Ensure cursor is set to CrossCursor (called after show and on focus/enter events)
    void ensureCrossCursor();

    // Multi-region capture access (for CaptureManager)
    bool isMultiRegionCapture() const;
    MultiRegionManager* multiRegionManager() const { return m_multiRegionManager; }
    const QPixmap& backgroundPixmap() const { return m_backgroundPixmap; }

signals:
    void regionSelected(const QPixmap &screenshot, const QPoint &globalPosition, const QRect &globalRect);
    void selectionCancelled();
    void saveRequested(const QPixmap &screenshot);
    void saveCompleted(const QPixmap &screenshot, const QString &filePath);
    void saveFailed(const QString &filePath, const QString &error);
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
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
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

    void syncMagnifierOverlay();

    // Corner radius helpers
    void onCornerRadiusChanged(int radius);
    int effectiveCornerRadius() const;

    // Toolbar helpers
    void handleToolbarClick(ToolId tool);

    // Color and style actions
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();
    void syncColorToRuntimeState(const QColor& color);
    void syncColorToAllWidgets(const QColor &color);

    // Line width callback
    void onLineWidthChanged(int width);

    // Lazy component creation
    void showEmojiPickerPopup();
    OCRManager* ensureOCRManager();
    QRCodeManager* ensureQRCodeManager();
    LoadingSpinnerRenderer* ensureLoadingSpinner();

    // Step Badge size handling
    void onStepBadgeSizeChanged(StepBadgeSize size);

    // Annotation settings handlers
    void onArrowStyleChanged(LineEndStyle style);
    void onLineStyleChanged(LineStyle style);

    // Window detection
    void updateWindowDetection(const QPoint &localPos);
    void setMultiRegionMode(bool enabled);
    void completeMultiRegionCapture();
    void cancelMultiRegionCapture();
    void copyToClipboard();
    void saveToFile();
    void shareToUrl();
    void finishSelection();
    bool ensureAutoBlurReadyForExport();
    void updateToolbarAutoBlurState();
    void finalizePolylineForToolbarInteraction();

    // Cursor helpers
    void setToolCursor();
    void hideShortcutHints();
    void positionRegionControlPanel();
    void positionMultiRegionListPanel();
    void hideSelectionFloatingUi(bool preserveToolState);
    void updateCompletedSelectionDragUiSuppression();
    bool completedSelectionDragUiSuppressed() const;
    void applyCanvasGeometry(const QSize& logicalSize);
    void maybeDismissShortcutHintsAfterSelectionCompleted();
    void updateShortcutHintsHoverVisibilityDuringSelection(const QPoint& localPos);
    static bool isPureModifierKey(int key);
    bool canNavigateHistoryReplay() const;
    void navigateHistoryReplay(int direction);
    void snapshotLiveReplaySlot();
    void restoreLiveReplaySlot();
    bool loadHistoryReplayIndex(int index);
    bool applyHistoryReplayEntry(const SnapTray::HistoryEntry& entry);
    void clearHistoryReplaySelectionState();
    void recordCaptureSession(const QPixmap& resultPixmap);
    void recordCaptureSession(const QImage& resultImage);
    struct HistoryCaptureSnapshot;
    struct PendingHistorySubmission;
    std::optional<HistoryCaptureSnapshot> makeHistoryCaptureSnapshot(
        const QDateTime& createdAt = QDateTime::currentDateTime()) const;
    void submitPendingHistorySubmission(PendingHistorySubmission submission) const;
    QRect currentHistorySelectionRect() const;
    QVector<MultiRegionManager::Region> currentHistoryCaptureRegions() const;

    // Initialization helpers
    void setupScreenGeometry(QScreen* screen);
    void handleCursorScreenSwitch();
    void preserveCompletedSelectionSnapshot();
    void clearPreservedSelection();
    void finishPreservedSelection();
    void refreshWindowDetectorForCurrentScreen();
    void refreshMultiRegionListPanel();
    void beginMultiRegionReplace(int index);
    void cancelMultiRegionReplace(bool restoreSelection);
    void syncRegionSubToolbar(bool refreshContent);
    void resetInitialRevealState();
    void beginInitialRevealPreparation(WindowDetector::QueryMode initialQueryMode);
    void maybeStartInitialRevealWait();
    void handleInitialRevealDetectorReady();
    void handleInitialRevealTimeout();
    void applyInitialWindowDetection(WindowDetector::QueryMode queryMode);
    void commitInitialReveal();
    void scheduleInitialRevealRefinement();
    void refreshWindowDetectionAtCursor(WindowDetector::QueryMode queryMode);
    void updateWindowDetection(const QPoint &localPos, WindowDetector::QueryMode queryMode);

    // Screen lifecycle management
    void onScreenRemoved(QScreen *screen);
    bool isScreenValid() const;

    // Annotation helpers
    bool isAnnotationTool(ToolId tool) const;
    bool shouldShowMagnifier() const;
    void syncFloatingUiCursor();
    void restoreRegionCursorAt(const QPoint& localPos);
    void hideDetachedFloatingUi();
    void restoreAfterDialogCancelled();
    bool hasBlockingTransientUiOpen() const;
    void trackBlockingDialog(SnapTray::QmlDialog* dialog);
    SnapTray::QmlDialog* createTransientDialog(const QUrl& qmlSource,
                                               QObject* viewModel,
                                               const QString& contextPropertyName);
    SnapTray::QmlEmojiPickerPopup* createEmojiPickerPopup();
    void syncSelectionToolbarHoverState(const QPoint& globalPos);
    bool isCursorOverSelectionToolbar(const QPoint& globalPos) const;
    bool isGlobalPosOverFloatingUi(const QPoint& globalPos) const;

    // Inline text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);

    // Text formatting signal handlers
    void onFontSizeDropdownRequested(const QPoint& pos);
    void onFontFamilyDropdownRequested(const QPoint& pos);
    void onFontSizeSelected(int size);
    void onFontFamilySelected(const QString& family);

    // Screen coordinate conversion helpers
    QPoint localToGlobal(const QPoint& localPos) const;
    QPoint globalToLocal(const QPoint& globalPos) const;
    QRect localToGlobal(const QRect& localRect) const;
    QRect globalToLocal(const QRect& globalRect) const;
    QRect floatingToolbarRectInLocalCoords() const;

    QPixmap m_backgroundPixmap;
    SharedPixmap m_sharedSourcePixmap;  // Shared for mosaic tool memory efficiency

    RegionInputState m_inputState;
    QPointer<QScreen> m_currentScreen;

    // Selection state manager
    SelectionStateManager *m_selectionManager;
    qreal m_devicePixelRatio;

    // QML toolbar (floating QQuickView windows)
    RegionToolbarViewModel* m_toolbarViewModel = nullptr;
    PinToolOptionsViewModel* m_toolOptionsViewModel = nullptr;
    std::unique_ptr<SnapTray::QmlFloatingToolbar> m_qmlToolbar;
    std::unique_ptr<SnapTray::QmlFloatingSubToolbar> m_qmlSubToolbar;
    bool m_toolbarUserDragged = false;
    bool m_cursorOverSelectionToolbar = false;

    // Emoji picker (QML popup)
    SnapTray::QmlEmojiPickerPopup *m_emojiPickerPopup = nullptr;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer; // Qt parent owns lifetime
    ToolManager *m_toolManager;
    std::unique_ptr<AnnotationSurfaceAdapter> m_annotationSurfaceAdapter;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // Selection state flags
    bool m_isClosing;
    bool m_saveDialogOpen;
    bool m_dropdownOpen;
    int m_openBlockingDialogCount = 0;

    // Window detection state
    WindowDetector *m_windowDetector;
    std::optional<DetectedElement> m_detectedWindow;

    // OCR state
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
    bool m_shareInProgress = false;
    bool m_exportInProgress = false;
    QString m_pendingSharePassword;
    LoadingSpinnerRenderer *m_loadingSpinner = nullptr;
    SnapTray::QmlToast *m_selectionToast = nullptr;

    void performOCR();
    void onOCRComplete(const OCRResult &result);
    void showOCRResultDialog(const OCRResult &result);

    // QR Code state
    QRCodeManager *m_qrCodeManager;
    bool m_qrCodeInProgress;

    void performQRCodeScan();
    void onQRCodeComplete(bool success, const QString &text, const QString &format, const QString &error, const QPixmap &sourceImage);

    // Auto-blur detection
    AutoBlurManager *m_autoBlurManager;
    bool m_autoBlurInProgress;
    QPointer<QFutureWatcherBase> m_autoBlurWatcher;

    void performAutoBlur();
    void onAutoBlurComplete(bool success, int faceCount, int credentialCount, const QString &error);

    // Inline text editing
    InlineTextEditor *m_textEditor;

    // Text annotation editor component (handles editing, transformation, formatting, dragging)
    TextAnnotationEditor *m_textAnnotationEditor;

    // Shape annotation editor component (handles transform/drag interactions).
    std::unique_ptr<ShapeAnnotationEditor> m_shapeAnnotationEditor;

    // Color picker dialog
    std::unique_ptr<snaptray::colorwidgets::ColorPickerDialogCompat> m_colorPickerDialog;

    // Startup protection - track window activation for robust deactivate handling
    int m_activationCount = 0;

    // Magnifier panel component
    MagnifierPanel* m_magnifierPanel;
    std::unique_ptr<MagnifierOverlay> m_magnifierOverlay;

    // Keep shortcut hints painter-based and in-window. A prior QML top-level
    // overlay version regressed on macOS by flashing and disappearing during
    // RegionSelector activation/screen-overlay transitions.
    enum class InitialRevealState {
        Preparing,
        ReadyToReveal,
        Revealed
    };

    bool m_showShortcutHintsOnEntry = false;
    bool m_shortcutHintsVisible = false;
    bool m_shortcutHintsTemporarilyHiddenByHover = false;
    std::unique_ptr<CaptureShortcutHintsOverlay> m_shortcutHintsOverlay;
    InitialRevealState m_initialRevealState = InitialRevealState::Revealed;
    WindowDetector::QueryMode m_initialWindowDetectionMode = WindowDetector::QueryMode::IncludeChildControls;
    QPoint m_initialRevealCursorPos;
    quint64 m_initialRevealToken = 0;
    QTimer* m_initialRevealTimer = nullptr;
    QMetaObject::Connection m_initialRevealWindowListReadyConnection;
    QElapsedTimer m_initialRevealPerfTimer;
    bool m_initialRevealPerfTimerActive = false;
    bool m_initialRevealDetectorPending = false;

    // Update throttling component
    UpdateThrottler m_updateThrottler;

    // Dirty region tracking for partial updates
    QRect m_lastSelectionRect;  // Previous selection rect for dirty region calculation
    QRect m_lastMagnifierRect;  // Previous magnifier rect

    // Screen switch monitoring
    QTimer* m_screenSwitchTimer = nullptr;

    struct HistoryCaptureSnapshot {
        QPixmap backgroundPixmap;
        QRect selectionRect;
        QVector<MultiRegionManager::Region> captureRegions;
        QByteArray annotationsJson;
        qreal devicePixelRatio = 1.0;
        QSize canvasLogicalSize;
        int cornerRadius = 0;
        int maxEntries = 20;
        QDateTime createdAt;
    };

    struct PendingHistorySubmission {
        HistoryCaptureSnapshot snapshot;
        QImage resultImage;
    };

    struct HistoryLiveSlot {
        bool valid = false;
        QSize canvasLogicalSize;
        QPixmap backgroundPixmap;
        qreal devicePixelRatio = 1.0;
        QRect selectionRect;
        bool multiRegionMode = false;
        QVector<MultiRegionManager::Region> multiRegions;
        QByteArray annotationsJson;
        int cornerRadius = 0;
    };

    QList<SnapTray::HistoryEntry> m_historyReplayEntries;
    HistoryLiveSlot m_historyLiveSlot;
    int m_historyReplayIndex = -1;  // -1 = current live slot
    bool m_historyReplayActive = false;
    std::optional<PendingHistorySubmission> m_pendingShareSubmission;

    // Preserve completed selection when cursor switches to another screen.
    bool m_hasPreservedSelection = false;
    QRect m_preservedGlobalSelectionRect;
    QPixmap m_preservedSelectionPixmap;

    // Region control panel (QML - radius + aspect ratio)
    RegionControlViewModel* m_regionControlViewModel = nullptr;
    std::unique_ptr<SnapTray::QmlOverlayPanel> m_regionControlPanel;
    int m_cornerRadius = 0;  // Current corner radius in logical pixels

    // Multi-region capture
    MultiRegionManager* m_multiRegionManager = nullptr;       // Qt parent owns lifetime
    MultiRegionListViewModel* m_multiRegionListViewModel = nullptr;
    std::unique_ptr<SnapTray::QmlOverlayPanel> m_multiRegionListPanel;
    QRect m_replaceOriginalRect;
    bool m_multiRegionListRefreshPending = false;

    // Quick Pin mode (select region and pin immediately, skip toolbar)
    bool m_quickPinMode = false;

    // Painting component
    RegionPainter* m_painter;

    // Input handling component
    RegionInputHandler* m_inputHandler;

    // Toolbar handling component
    RegionToolbarHandler* m_toolbarHandler;

    // Settings helper component
    RegionSettingsHelper* m_settingsHelper;

    // Export manager component
    RegionExportManager* m_exportManager;
    ShareUploadClient* m_shareClient = nullptr;

    // Shared annotation setup/signals helper
    std::unique_ptr<AnnotationContext> m_annotationContext;

    std::function<SnapTray::QmlDialog*(const QUrl&, QObject*, const QString&, QObject*)>
        m_dialogFactory;
    std::function<SnapTray::QmlEmojiPickerPopup*(QObject*)> m_emojiPickerFactory;
    std::function<std::unique_ptr<snaptray::colorwidgets::ColorPickerDialogCompat>()>
        m_colorPickerDialogFactory;
    std::function<void()> m_restoreAfterDialogCancelledHook;
    std::function<bool(const QImage&)> m_guiClipboardWriter;
};

#endif // REGIONSELECTOR_H
