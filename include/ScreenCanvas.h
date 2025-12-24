#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>

class QScreen;
class AnnotationLayer;
class AnnotationController;
class ColorPaletteWidget;
class ColorPickerDialog;
class LineWidthWidget;
class ColorAndWidthWidget;
class LaserPointerRenderer;
class ClickRippleRenderer;

// Canvas tool types (simplified subset for screen canvas)
enum class CanvasTool {
    Pencil = 0,
    Marker,
    Arrow,
    Rectangle,
    Ellipse,
    LaserPointer,
    CursorHighlight,
    Undo,
    Redo,
    Clear,
    Exit,
    Count  // Total number of buttons
};

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
    QString getIconKeyForTool(CanvasTool tool) const;
    void renderIcon(QPainter &painter, const QRect &rect, CanvasTool tool, const QColor &color);
    void updateToolbarPosition();
    int getButtonAtPosition(const QPoint &pos);
    void handleToolbarClick(CanvasTool button);
    QString getButtonTooltip(int buttonIndex);
    bool isAnnotationTool(CanvasTool tool) const;

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

    // Screen capture
    QPixmap m_backgroundPixmap;
    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and controller
    AnnotationLayer *m_annotationLayer;
    AnnotationController *m_controller;
    CanvasTool m_currentTool;

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
