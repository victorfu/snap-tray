#ifndef REGIONSELECTOR_H
#define REGIONSELECTOR_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <QSettings>
#include <QPointer>
#include <memory>
#include <optional>

#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/StepBadgeAnnotation.h"
#include "toolbar/ToolbarCore.h"
#include "InlineTextEditor.h"
#include "WindowDetector.h"
#include "LoadingSpinnerRenderer.h"
#include "TransformationGizmo.h"
#include "TextFormattingState.h"
#include "annotation/AnnotationHostAdapter.h"
#include "tools/ToolId.h"
#include "tools/ToolTraits.h"
#include "tools/ToolManager.h"
#include "region/SelectionStateManager.h"
#include "region/MagnifierPanel.h"
#include "region/UpdateThrottler.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionControlWidget.h"
#include "region/MultiRegionManager.h"
#include "region/RegionInputState.h"
#include "ui/sections/MosaicBlurTypeSection.h"

class QScreen;
class ToolOptionsPanel;
class EmojiPicker;

namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}
class QCloseEvent;
class OCRManager;
struct OCRResult;
class QRCodeManager;
class AutoBlurManager;
class RegionPainter;
class RegionInputHandler;
class RegionToolbarHandler;
class RegionSettingsHelper;
class RegionExportManager;
class AnnotationContext;

// ShapeType and ShapeFillMode are defined in annotations/ShapeAnnotation.h

// Helper to check if a tool is handled by ToolManager
// (vs. custom handling like Text which has special UI needs)
inline bool isToolManagerHandledTool(ToolId id) {
    return ToolTraits::isToolManagerHandledTool(id);
}

class RegionSelector : public QWidget, public AnnotationHostAdapter
{
    Q_OBJECT

public:
    explicit RegionSelector(QWidget *parent = nullptr);
    ~RegionSelector();

    // 初始化指定螢幕的截圖 (由 CaptureManager 調用)
    void initializeForScreen(QScreen *screen, const QPixmap &preCapture = QPixmap());

    // 初始化指定螢幕並使用預設區域 (用於錄影取消後返回)
    void initializeWithRegion(QScreen *screen, const QRect &region);

    // 設置 Quick Pin 模式 (選擇區域後直接 pin，不顯示 toolbar)
    void setQuickPinMode(bool enabled);

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
    void enterEvent(QEnterEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

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

    void drawMagnifier(QPainter &painter);

    // Corner radius helpers
    void onCornerRadiusChanged(int radius);
    int effectiveCornerRadius() const;

    // Toolbar helpers
    void handleToolbarClick(ToolId tool);

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();
    void syncColorToAllWidgets(const QColor &color);

    // Line width callback
    void onLineWidthChanged(int width);

    // Lazy component creation
    EmojiPicker* ensureEmojiPicker();
    OCRManager* ensureOCRManager();
    QRCodeManager* ensureQRCodeManager();
    LoadingSpinnerRenderer* ensureLoadingSpinner();

    // Step Badge size handling
    void onStepBadgeSizeChanged(StepBadgeSize size);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;
    int toolWidthForCurrentTool() const;

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
    void finishSelection();

    // Cursor helpers
    QCursor getMosaicCursor(int width);
    void setToolCursor();

    // Initialization helpers
    void setupScreenGeometry(QScreen* screen);

    // Screen lifecycle management
    void onScreenRemoved(QScreen *screen);
    bool isScreenValid() const;

    // Annotation helpers
    bool isAnnotationTool(ToolId tool) const;

    // Inline text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);
    void startTextReEditing(int annotationIndex);

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

    QPixmap m_backgroundPixmap;
    SharedPixmap m_sharedSourcePixmap;  // Shared for mosaic tool memory efficiency

    RegionInputState m_inputState;
    QPointer<QScreen> m_currentScreen;

    // Selection state manager
    SelectionStateManager *m_selectionManager;
    qreal m_devicePixelRatio;

    // Toolbar
    ToolbarCore *m_toolbar;

    // Unified color and width widget
    ToolOptionsPanel *m_colorAndWidthWidget;

    // Emoji picker
    EmojiPicker *m_emojiPicker;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;
    MosaicBlurTypeSection::BlurType m_mosaicBlurType = MosaicBlurTypeSection::BlurType::Pixelate;

    // Mosaic cursor cache (avoid recreating on every mouse move)
    QCursor m_mosaicCursorCache;
    int m_mosaicCursorCacheWidth = -1;

    // Selection state flags
    bool m_isClosing;
    bool m_isDialogOpen;  // Prevents close during file dialog

    // Window detection state
    WindowDetector *m_windowDetector;
    std::optional<DetectedElement> m_detectedWindow;

    // OCR state
    OCRManager *m_ocrManager;
    bool m_ocrInProgress;
    LoadingSpinnerRenderer *m_loadingSpinner = nullptr;
    class QLabel *m_ocrToastLabel;
    class QTimer *m_ocrToastTimer;

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

    void performAutoBlur();
    void onAutoBlurComplete(bool success, int faceCount, int textCount, const QString &error);

    // Inline text editing
    InlineTextEditor *m_textEditor;

    // Text annotation editor component (handles editing, transformation, formatting, dragging)
    TextAnnotationEditor *m_textAnnotationEditor;

    // Color picker dialog
    snaptray::colorwidgets::ColorPickerDialogCompat *m_colorPickerDialog;

    // Startup protection - track window activation for robust deactivate handling
    int m_activationCount = 0;

    // Magnifier panel component
    MagnifierPanel* m_magnifierPanel;

    // Update throttling component
    UpdateThrottler m_updateThrottler;

    // Dirty region tracking for partial updates
    QRect m_lastSelectionRect;  // Previous selection rect for dirty region calculation
    QRect m_lastMagnifierRect;  // Previous magnifier rect

    // Region control widget (radius + aspect ratio)
    RegionControlWidget* m_regionControlWidget = nullptr;
    int m_cornerRadius = 0;  // Current corner radius in logical pixels

    // Multi-region capture
    MultiRegionManager* m_multiRegionManager = nullptr;

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

    // Shared annotation setup/signals helper
    std::unique_ptr<AnnotationContext> m_annotationContext;
};

#endif // REGIONSELECTOR_H
