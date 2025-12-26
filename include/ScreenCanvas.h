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
#include "tools/ToolId.h"

class QScreen;
class AnnotationLayer;
class ToolManager;
class ColorPaletteWidget;
class ColorPickerDialog;
class LineWidthWidget;
class ColorAndWidthWidget;
class LaserPointerRenderer;
class ClickRippleRenderer;

// Canvas toolbar button IDs (for UI display only)
// These represent button positions in the toolbar
enum class CanvasButton {
    Pencil = 0,
    Marker,
    Arrow,
    Shape,          // Unified Rectangle/Ellipse
    LaserPointer,
    CursorHighlight,
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
    case CanvasButton::LaserPointer: return ToolId::LaserPointer;
    case CanvasButton::CursorHighlight: return ToolId::CursorHighlight;
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
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // Painting helpers
    void drawToolbar(QPainter &painter);
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void drawTooltip(QPainter &painter);
    void drawCursorDot(QPainter &painter);

    // Toolbar helpers
    void initializeIcons();
    QString getIconKeyForButton(CanvasButton button) const;
    void renderIcon(QPainter &painter, const QRect &rect, CanvasButton button, const QColor &color);
    void updateToolbarPosition();
    int getButtonAtPosition(const QPoint &pos);
    void handleToolbarClick(CanvasButton button);
    QString getButtonTooltip(int buttonIndex);
    bool isDrawingTool(ToolId toolId) const;

    // Color palette helpers (legacy)
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Line width helpers (legacy)
    bool shouldShowLineWidthWidget() const;
    void onLineWidthChanged(int width);

    // Unified color and width widget helpers
    bool shouldShowColorAndWidthWidget() const;
    bool shouldShowWidthControl() const;

    // Annotation settings persistence
    QColor loadAnnotationColor() const;
    void saveAnnotationColor(const QColor &color);
    int loadAnnotationWidth() const;
    void saveAnnotationWidth(int width);
    LineEndStyle loadArrowStyle() const;
    void saveArrowStyle(LineEndStyle style);
    void onArrowStyleChanged(LineEndStyle style);

    // Screen capture
    QPixmap m_backgroundPixmap;
    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and tool manager
    AnnotationLayer *m_annotationLayer;
    ToolManager *m_toolManager;
    ToolId m_currentToolId;

    // Toolbar
    QRect m_toolbarRect;
    int m_hoveredButton;
    QVector<QRect> m_buttonRects;

    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;

    // Color palette
    ColorPaletteWidget *m_colorPalette;

    // Line width widget
    LineWidthWidget *m_lineWidthWidget;

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
};

#endif // SCREENCANVAS_H
