#ifndef PINWINDOWANNOTATIONCONTROLLER_H
#define PINWINDOWANNOTATIONCONTROLLER_H

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QColor>
#include <memory>

class QWidget;
class QPainter;
class QPixmap;
class AnnotationLayer;
class ToolManager;
class ColorAndWidthWidget;
class InlineTextEditor;
class TextAnnotationEditor;

/**
 * @brief Controller for managing annotations in PinWindow.
 *
 * This class coordinates all annotation-related functionality including:
 * - Tool management via ToolManager
 * - Annotation storage via AnnotationLayer
 * - Coordinate transformation between screen and image space
 * - Text annotation editing
 * - Color/width settings
 */
class PinWindowAnnotationController : public QObject
{
    Q_OBJECT

public:
    explicit PinWindowAnnotationController(QWidget* parent = nullptr);
    ~PinWindowAnnotationController() override;

    // Initialization
    void setSourcePixmap(const QPixmap* pixmap);
    void setDevicePixelRatio(qreal dpr);

    // Transformation parameters (for coordinate conversion)
    void setZoomLevel(qreal zoom);
    void setRotationAngle(int angle);
    void setFlipState(bool horizontal, bool vertical);

    // Annotation mode
    bool isAnnotationMode() const { return m_annotationMode; }
    void setAnnotationMode(bool enabled);

    // Tool management
    void setCurrentTool(int toolId);
    int currentTool() const;
    bool isDrawing() const;

    // Color and width
    void setColor(const QColor& color);
    QColor color() const;
    void setWidth(int width);
    int width() const;

    // Mouse event handling (in screen coordinates)
    void handleMousePress(const QPoint& screenPos);
    void handleMouseMove(const QPoint& screenPos);
    void handleMouseRelease(const QPoint& screenPos);
    void handleDoubleClick(const QPoint& screenPos);

    // Drawing
    void draw(QPainter& painter) const;
    void drawOntoPixmap(QPixmap& pixmap) const;

    // Text annotation support
    bool isTextEditing() const;
    void cancelTextEditing();

    // Undo/Redo
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // State
    bool hasAnnotations() const;

    // Access to sub-components (for toolbar integration)
    ColorAndWidthWidget* colorAndWidthWidget() const { return m_colorAndWidthWidget.get(); }
    AnnotationLayer* annotationLayer() const { return m_annotationLayer.get(); }

signals:
    void annotationModeChanged(bool enabled);
    void toolChanged(int toolId);
    void needsRepaint();
    void undoRedoStateChanged();

private:
    // Coordinate transformation
    QPoint screenToImage(const QPoint& screenPos) const;
    QPoint imageToScreen(const QPoint& imagePos) const;

    // Create painter transform for drawing annotations
    void setupPainterTransform(QPainter& painter) const;

    // Components
    std::unique_ptr<AnnotationLayer> m_annotationLayer;
    std::unique_ptr<ToolManager> m_toolManager;
    std::unique_ptr<ColorAndWidthWidget> m_colorAndWidthWidget;
    std::unique_ptr<InlineTextEditor> m_textEditor;
    std::unique_ptr<TextAnnotationEditor> m_textAnnotationEditor;

    // State
    bool m_annotationMode = false;
    QWidget* m_parentWidget = nullptr;

    // Transformation state
    qreal m_zoomLevel = 1.0;
    int m_rotationAngle = 0;
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;
    qreal m_devicePixelRatio = 1.0;
    const QPixmap* m_sourcePixmap = nullptr;
};

#endif // PINWINDOWANNOTATIONCONTROLLER_H
