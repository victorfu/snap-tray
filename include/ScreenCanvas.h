#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>
#include <QPointer>
#include <memory>
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotation/AnnotationHostAdapter.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionSettingsHelper.h"
#include "tools/ToolId.h"
#include "ToolbarStyle.h"
#include "TransformationGizmo.h"

class QScreen;
class AnnotationLayer;
class ToolManager;
namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}
class EmojiPicker;
class ToolOptionsPanel;
class LaserPointerRenderer;
class ToolbarCore;
class ArrowAnnotation;
class PolylineAnnotation;
class TextBoxAnnotation;
class AnnotationContext;

// Canvas background mode
enum class CanvasBackgroundMode {
    Screen,     // Screenshot background (default)
    Whiteboard, // Solid white background
    Blackboard  // Solid black background
};

class ScreenCanvas : public QWidget, public AnnotationHostAdapter
{
    Q_OBJECT

public:
    explicit ScreenCanvas(QWidget *parent = nullptr);
    ~ScreenCanvas();

    void initializeForScreen(QScreen *screen);

signals:
    void closed();

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

    // Painting helpers
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void drawCursorDot(QPainter &painter);

    // Toolbar helpers
    void initializeIcons();
    void setupToolbar();
    void updateToolbarPosition();
    void handleToolbarClick(int buttonId);
    bool isDrawingTool(ToolId toolId) const;
    QColor getButtonIconColor(int buttonId) const;
    void setToolCursor();

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Line width callback
    void onLineWidthChanged(int width);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;
    void syncColorToAllWidgets(const QColor& color);

    // Annotation settings persistence
    void onArrowStyleChanged(LineEndStyle style);
    void onLineStyleChanged(LineStyle style);

    // Text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);

    // Font dropdown handlers
    void onFontSizeDropdownRequested(const QPoint &pos);
    void onFontFamilyDropdownRequested(const QPoint &pos);
    void onFontSizeSelected(int size);
    void onFontFamilySelected(const QString &family);

    // Background pixmap (used for Whiteboard/Blackboard modes)
    QPixmap m_backgroundPixmap;
    QPointer<QScreen> m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    ToolId m_currentToolId;

    // Toolbar using ToolbarCore
    ToolbarCore *m_toolbar;
    bool m_isDraggingToolbar;
    bool m_toolbarManuallyPositioned;
    QPoint m_toolbarDragOffset;
    bool m_showSubToolbar;

    // Unified color and width widget
    ToolOptionsPanel *m_colorAndWidthWidget;

    // Emoji picker
    EmojiPicker *m_emojiPicker;

    // Color picker dialog
    snaptray::colorwidgets::ColorPickerDialogCompat *m_colorPickerDialog;

    // Laser pointer renderer
    LaserPointerRenderer *m_laserRenderer;

    // Cursor position for drawing cursor dot
    QPoint m_cursorPos;

    // Toolbar style configuration
    ToolbarStyleConfig m_toolbarStyleConfig;

    // Shape tool state
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;

    // StepBadge tool state
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // Arrow and Polyline editing state
    bool m_isArrowDragging = false;
    GizmoHandle m_arrowDragHandle = GizmoHandle::None;
    bool m_isPolylineDragging = false;
    int m_activePolylineVertexIndex = -1;
    QPoint m_dragStartPos;
    bool m_consumeNextToolRelease = false;

    // Arrow and Polyline helpers
    TextBoxAnnotation* getSelectedTextAnnotation();

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

    // Text editing components

    // Text editing components
    InlineTextEditor *m_textEditor;
    TextAnnotationEditor *m_textAnnotationEditor;
    RegionSettingsHelper *m_settingsHelper;

    // Background mode
    CanvasBackgroundMode m_bgMode = CanvasBackgroundMode::Screen;

    // Shared annotation setup/signals helper
    std::unique_ptr<AnnotationContext> m_annotationContext;

    // Background mode helpers
    void setBackgroundMode(CanvasBackgroundMode mode);
    QPixmap createSolidBackgroundPixmap(const QColor& color) const;
};

#endif // SCREENCANVAS_H
