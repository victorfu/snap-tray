#ifndef SCREENCANVASSESSION_H
#define SCREENCANVASSESSION_H

#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QPixmap>
#include <QRect>
#include <functional>
#include <map>
#include <memory>

#include "ScreenCanvas.h"
#include "settings/ScreenCanvasSettingsManager.h"
#include "tools/ToolId.h"
#include "TransformationGizmo.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"

class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QScreen;
class QWheelEvent;
class AnnotationLayer;
class AnnotationContext;
class ArrowAnnotation;
class CanvasToolbarViewModel;
class EmojiStickerAnnotation;
class InlineTextEditor;
class LaserPointerRenderer;
class PinToolOptionsViewModel;
class PolylineAnnotation;
class RegionSettingsHelper;
class ShapeAnnotation;
class TextAnnotationEditor;
class TextBoxAnnotation;
class ToolManager;

namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}

namespace SnapTray {
class QmlEmojiPickerPopup;
class QmlFloatingSubToolbar;
class QmlFloatingToolbar;
}

class ScreenCanvasSession : public QObject
{
    Q_OBJECT

    friend class TestScreenCanvasPlacement;
    friend class TestScreenCanvasSessionRecovery;
    friend class TestScreenCanvasCopyExport;

public:
    struct ToolbarPlacementResolution
    {
        QString screenId;
        QPoint globalTopLeft;
        QPoint topLeftInScreen;
        bool usedStoredPlacement = false;
    };

    explicit ScreenCanvasSession(QObject* parent = nullptr);
    ~ScreenCanvasSession() override;

    void open(QScreen* activationScreen);
    void close();
    bool isOpen() const { return m_isOpen; }
    int surfaceCount() const;

    static QRect virtualDesktopGeometry(const QList<QScreen*>& screens);
    static QPoint globalToDesktop(const QPoint& globalPos, const QRect& desktopGeometry);
    static QPoint desktopToLocal(const QPoint& desktopPos,
                                 const QRect& screenGeometry,
                                 const QRect& desktopGeometry);
    static QPoint clampToolbarTopLeftToBounds(const QPoint& desiredTopLeft,
                                              const QRect& bounds,
                                              const QSize& toolbarSize);
    static QPoint defaultToolbarTopLeftForScreen(const QRect& screenBounds,
                                                 const QSize& toolbarSize);
    static ToolbarPlacementResolution resolveToolbarPlacement(
        const ScreenCanvasToolbarPlacement& savedPlacement,
        const QList<QScreen*>& screens,
        QScreen* activationScreen,
        const QSize& toolbarSize);

    void handleSurfacePaint(ScreenCanvas* surface, QPainter& painter);
    void handleSurfaceMousePress(ScreenCanvas* surface, QMouseEvent* event);
    void handleSurfaceMouseMove(ScreenCanvas* surface, QMouseEvent* event);
    void handleSurfaceMouseRelease(ScreenCanvas* surface, QMouseEvent* event);
    void handleSurfaceMouseDoubleClick(ScreenCanvas* surface, QMouseEvent* event);
    void handleSurfaceWheel(ScreenCanvas* surface, QWheelEvent* event);
    void handleSurfaceKeyPress(ScreenCanvas* surface, QKeyEvent* event);
    void handleSurfaceCloseRequest(ScreenCanvas* surface);

signals:
    void closed();

private:
    using ToolbarClickHandler = void (ScreenCanvasSession::*)(ToolId);

    static const std::map<ToolId, ToolbarClickHandler>& toolbarDispatchTable();
    static const std::map<ToolId, ToolbarClickHandler>& actionDispatchTable();

    void initializeSharedState();
    void teardown();
    void createSurfaces(const QList<QScreen*>& screens);
    void configureSurface(ScreenCanvas* surface);
    void destroySurfaces();
    void updateAllSurfaces();
    void updateSurfacesForAnnotationRect(const QRect& annotationRect);
    bool hasActiveAnnotationInteraction() const;
    QRect selectedAnnotationInteractionRect() const;
    void requestLocalizedToolRepaint();
    void resetAnnotationInteractionTracking();
    struct FloatingUiVisibilityState {
        bool toolbarVisible = false;
        bool emojiPickerVisible = false;
    };
    struct ScreenSnapshotVisibilityState {
        FloatingUiVisibilityState floatingUi;
        QList<QPointer<ScreenCanvas>> visibleSurfaces;
    };
    FloatingUiVisibilityState hideFloatingUiForCapture();
    void restoreFloatingUiAfterCapture(const FloatingUiVisibilityState& state);
    ScreenSnapshotVisibilityState hideUiForScreenSnapshot();
    void restoreUiAfterScreenSnapshot(const ScreenSnapshotVisibilityState& state);

    void activateSurface(ScreenCanvas* surface);
    void beginMouseGrab(ScreenCanvas* surface);
    void endMouseGrab();
    ScreenCanvas* editingSurface() const;
    ScreenCanvas* toolbarHostSurface() const;
    ScreenCanvas* surfaceForGlobalPoint(const QPoint& globalPos) const;
    ScreenCanvas* surfaceForScreen(QScreen* screen) const;
    RegionSettingsHelper* activeSettingsHelper() const;
    void refreshFloatingUiParentWidget();

    void drawAnnotations(ScreenCanvas* surface, QPainter& painter);
    void drawCurrentAnnotation(QPainter& painter);

    void handleToolbarClick(int buttonId);
    void handlePersistentToolClick(ToolId toolId);
    void handleLaserPointerClick();
    void handleCanvasModeToggle(ToolId toolId);
    void handleActionToolClick(ToolId toolId);
    void handleUndoAction(ToolId toolId);
    void handleRedoAction(ToolId toolId);
    void handleClearAction(ToolId toolId);
    void handleCopyAction(ToolId toolId);
    void handleExitAction(ToolId toolId);
    void finalizePolylineForToolbarInteraction();
    bool isDrawingTool(ToolId toolId) const;
    QScreen* resolveCopyTargetScreen() const;
    QPixmap buildCopyExportBasePixmap(QScreen* screen) const;
    QPixmap exportCanvasPixmapForScreen(QScreen* screen) const;

    void setToolCursor();
    void syncFloatingUiCursor(ScreenCanvas* surface);
    void restoreCanvasCursorAt(ScreenCanvas* surface, const QPoint& localPos);
    bool isGlobalPosOverFloatingUi(const QPoint& globalPos) const;
    void updateAnnotationCursor(ScreenCanvas* surface, const QPoint& annotationPos);

    void onColorSelected(const QColor& color);
    void onMoreColorsRequested();
    QPoint colorPickerAnchorPoint() const;
    void onLineWidthChanged(int width);
    void onArrowStyleChanged(LineEndStyle style);
    void onLineStyleChanged(LineStyle style);
    void onFontSizeDropdownRequested(const QPoint& pos);
    void onFontFamilyDropdownRequested(const QPoint& pos);
    void onFontSizeSelected(int size);
    void onFontFamilySelected(const QString& family);
    void onTextEditingFinished(ScreenCanvas* surface, const QString& text, const QPoint& position);
    void syncColorToAllWidgets(const QColor& color);
    void syncTextFormattingToSubToolbar();

    void updateQmlToolbarState();
    bool initializeToolbarPlacement();
    void relayoutFloatingUi(bool restoreToolbarPosition = false);
    void saveToolbarPlacement() const;
    void connectScreenTopologySignals();
    void connectApplicationStateSignal();
    void handleApplicationStateChanged(Qt::ApplicationState state);
    void restoreSurfaceVisibilityAndStacking(bool refocusActiveSurface);
    void raiseFloatingUiWindows();
    void closeForScreenTopologyChange(const QString& reason);
    void setBackgroundMode(CanvasBackgroundMode mode);
    void showEmojiPickerPopup();

    TextBoxAnnotation* getSelectedTextAnnotation();
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation();
    ShapeAnnotation* getSelectedShapeAnnotation();
    ArrowAnnotation* getSelectedArrowAnnotation();
    PolylineAnnotation* getSelectedPolylineAnnotation();

    bool handleEmojiStickerAnnotationPress(const QPoint& pos);
    bool handleEmojiStickerAnnotationMove(const QPoint& pos);
    bool handleEmojiStickerAnnotationRelease(const QPoint& pos);
    bool handleArrowAnnotationPress(const QPoint& pos);
    bool handleArrowAnnotationMove(const QPoint& pos);
    bool handleArrowAnnotationRelease(const QPoint& pos);
    bool handlePolylineAnnotationPress(const QPoint& pos);
    bool handlePolylineAnnotationMove(const QPoint& pos);
    bool handlePolylineAnnotationRelease(const QPoint& pos);

    QPoint annotationPointForEvent(ScreenCanvas* surface, QMouseEvent* event) const;
    QPoint annotationPointForCurrentCursor() const;

    QPointer<QScreen> m_activationScreen;
    QRect m_desktopGeometry;
    QList<QPointer<ScreenCanvas>> m_surfaces;
    QPointer<ScreenCanvas> m_activeSurface;
    QPointer<ScreenCanvas> m_grabbedSurface;

    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    ToolId m_currentToolId = ToolId::Pencil;
    bool m_laserPointerActive = false;

    CanvasToolbarViewModel* m_toolbarViewModel = nullptr;
    PinToolOptionsViewModel* m_toolOptionsViewModel = nullptr;
    std::unique_ptr<SnapTray::QmlFloatingToolbar> m_qmlToolbar;
    std::unique_ptr<SnapTray::QmlFloatingSubToolbar> m_qmlSubToolbar;
    SnapTray::QmlEmojiPickerPopup* m_emojiPickerPopup = nullptr;
    std::unique_ptr<snaptray::colorwidgets::ColorPickerDialogCompat> m_colorPickerDialog;
    LaserPointerRenderer* m_laserRenderer = nullptr;

    bool m_toolbarPlacementInitialized = false;
    bool m_toolbarDragging = false;
    bool m_skipToolbarPlacementSaveOnClose = false;
    bool m_screenTopologySignalsConnected = false;
    bool m_showSubToolbar = true;
    bool m_isOpen = false;
    bool m_isClosing = false;
    QMetaObject::Connection m_applicationStateChangedConnection;

    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;
    CanvasBackgroundMode m_bgMode = CanvasBackgroundMode::Screen;

    bool m_isEmojiDragging = false;
    bool m_isEmojiScaling = false;
    bool m_isEmojiRotating = false;
    GizmoHandle m_activeEmojiHandle = GizmoHandle::None;
    QPoint m_emojiDragStart;
    qreal m_emojiStartScale = 1.0;
    qreal m_emojiStartDistance = 0.0;
    QPointF m_emojiStartCenter;
    qreal m_emojiStartRotation = 0.0;
    qreal m_emojiStartAngle = 0.0;

    bool m_isArrowDragging = false;
    GizmoHandle m_arrowDragHandle = GizmoHandle::None;
    bool m_isPolylineDragging = false;
    int m_activePolylineVertexIndex = -1;
    QRect m_lastAnnotationInteractionRect;
    QPoint m_dragStartPos;
    bool m_consumeNextToolRelease = false;
    std::function<bool(const QImage&)> m_guiClipboardWriter;
    std::function<QPixmap(QScreen*)> m_screenSnapshotProvider;
};

#endif // SCREENCANVASSESSION_H
