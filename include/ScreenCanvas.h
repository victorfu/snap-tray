#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/StepBadgeAnnotation.h"
#include "ui/sections/MosaicBlurTypeSection.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "recording/SpotlightEffect.h"
#include "tools/ToolId.h"
#include "ToolbarStyle.h"

class QScreen;
class AnnotationLayer;
class ToolManager;
class ColorPaletteWidget;
class ColorPickerDialog;
class ColorAndWidthWidget;
class LaserPointerRenderer;
class ClickRippleRenderer;
class ToolbarWidget;

// Canvas toolbar button IDs (for UI display only)
// These represent button positions in the toolbar
enum class CanvasButton {
    Pencil = 0,
    Marker,
    Arrow,
    Shape,          // Unified Rectangle/Ellipse
    Mosaic,
    StepBadge,
    Text,
    LaserPointer,
    CursorHighlight,
    Spotlight,
    Undo,
    Redo,
    Clear,
    Exit,
    Count  // Total number of buttons
};

// Helper to map CanvasButton to ToolId
inline ToolId canvasButtonToToolId(CanvasButton btn) {
    switch (btn) {
    case CanvasButton::Pencil:       return ToolId::Pencil;
    case CanvasButton::Marker:       return ToolId::Marker;
    case CanvasButton::Arrow:        return ToolId::Arrow;
    case CanvasButton::Shape:        return ToolId::Shape;
    case CanvasButton::Mosaic:       return ToolId::Mosaic;
    case CanvasButton::StepBadge:    return ToolId::StepBadge;
    case CanvasButton::Text:         return ToolId::Text;
    case CanvasButton::LaserPointer: return ToolId::LaserPointer;
    case CanvasButton::CursorHighlight: return ToolId::CursorHighlight;
    case CanvasButton::Spotlight:    return ToolId::Spotlight;
    case CanvasButton::Undo:         return ToolId::Undo;
    case CanvasButton::Redo:         return ToolId::Redo;
    case CanvasButton::Clear:        return ToolId::Clear;
    case CanvasButton::Exit:         return ToolId::Exit;
    default:                         return ToolId::Selection;
    }
}

class ScreenCanvas : public QWidget
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

private:
    // Painting helpers
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void drawCursorDot(QPainter &painter);

    // Toolbar helpers
    void initializeIcons();
    void setupToolbar();
    void updateToolbarPosition();
    QString getIconKeyForButton(CanvasButton button) const;
    void renderIcon(QPainter &painter, const QRect &rect, CanvasButton button, const QColor &color);
    void handleToolbarClick(CanvasButton button);
    bool isDrawingTool(ToolId toolId) const;
    QColor getButtonIconColor(int buttonId) const;

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Line width callback
    void onLineWidthChanged(int width);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;

    // Annotation settings persistence
    LineEndStyle loadArrowStyle() const;
    void saveArrowStyle(LineEndStyle style);
    void onArrowStyleChanged(LineEndStyle style);
    LineStyle loadLineStyle() const;
    void saveLineStyle(LineStyle style);
    void onLineStyleChanged(LineStyle style);

    // Text editing handlers
    void onTextEditingFinished(const QString &text, const QPoint &position);

    // Screen capture
    QPixmap m_backgroundPixmap;
    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    ToolId m_currentToolId;

    // Toolbar using ToolbarWidget
    ToolbarWidget *m_toolbar;
    bool m_isDraggingToolbar;
    QPoint m_toolbarDragOffset;
    bool m_showSubToolbar;

    // Color palette
    ColorPaletteWidget *m_colorPalette;

    // Unified color and width widget
    ColorAndWidthWidget *m_colorAndWidthWidget;

    // Color picker dialog
    ColorPickerDialog *m_colorPickerDialog;

    // Laser pointer renderer
    LaserPointerRenderer *m_laserRenderer;

    // Click ripple renderer
    ClickRippleRenderer *m_rippleRenderer;

    // Cursor position for drawing cursor dot
    QPoint m_cursorPos;

    // Toolbar style configuration
    ToolbarStyleConfig m_toolbarStyleConfig;

    // Shape tool state
    ShapeType m_shapeType = ShapeType::Rectangle;
    ShapeFillMode m_shapeFillMode = ShapeFillMode::Outline;

    // Mosaic tool state
    MosaicBlurTypeSection::BlurType m_mosaicBlurType = MosaicBlurTypeSection::BlurType::Pixelate;

    // StepBadge tool state
    StepBadgeSize m_stepBadgeSize = StepBadgeSize::Medium;

    // Text editing components
    InlineTextEditor *m_textEditor;
    TextAnnotationEditor *m_textAnnotationEditor;

    // Spotlight effect
    SpotlightEffect *m_spotlightEffect;
};

#endif // SCREENCANVAS_H
