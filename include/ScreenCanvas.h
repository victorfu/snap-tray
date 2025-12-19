#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QWidget>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QVector>
#include <QColor>
#include <memory>

class QScreen;
class AnnotationLayer;
class PencilStroke;
class MarkerStroke;
class ArrowAnnotation;
class RectangleAnnotation;
class ColorPaletteWidget;

// Canvas tool types (simplified subset for screen canvas)
enum class CanvasTool {
    Pencil = 0,
    Marker,
    Arrow,
    Rectangle,
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
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // Painting helpers
    void drawToolbar(QPainter &painter);
    void drawAnnotations(QPainter &painter);
    void drawCurrentAnnotation(QPainter &painter);
    void drawTooltip(QPainter &painter);

    // Toolbar helpers
    void initializeIcons();
    QString getIconKeyForTool(CanvasTool tool) const;
    void renderIcon(QPainter &painter, const QRect &rect, CanvasTool tool, const QColor &color);
    void updateToolbarPosition();
    int getButtonAtPosition(const QPoint &pos);
    void handleToolbarClick(CanvasTool button);
    QString getButtonTooltip(int buttonIndex);
    bool isAnnotationTool(CanvasTool tool) const;

    // Color palette helpers
    bool shouldShowColorPalette() const;
    void onColorSelected(const QColor &color);
    void onMoreColorsRequested();

    // Annotation helpers
    void startAnnotation(const QPoint &pos);
    void updateAnnotation(const QPoint &pos);
    void finishAnnotation();

    // Screen capture
    QPixmap m_backgroundPixmap;
    QScreen *m_currentScreen;
    qreal m_devicePixelRatio;

    // Annotation layer and state
    AnnotationLayer *m_annotationLayer;
    CanvasTool m_currentTool;
    QColor m_annotationColor;
    int m_annotationWidth;

    // In-progress annotation state
    bool m_isDrawing;
    QPoint m_drawStartPoint;
    QVector<QPoint> m_currentPath;

    // Temporary annotation objects for preview
    std::unique_ptr<PencilStroke> m_currentPencil;
    std::unique_ptr<MarkerStroke> m_currentMarker;
    std::unique_ptr<ArrowAnnotation> m_currentArrow;
    std::unique_ptr<RectangleAnnotation> m_currentRectangle;

    // Toolbar
    QRect m_toolbarRect;
    int m_hoveredButton;
    QVector<QRect> m_buttonRects;

    static const int TOOLBAR_HEIGHT = 40;
    static const int BUTTON_WIDTH = 36;
    static const int BUTTON_SPACING = 2;

    // Color palette
    ColorPaletteWidget *m_colorPalette;
};

#endif // SCREENCANVAS_H
