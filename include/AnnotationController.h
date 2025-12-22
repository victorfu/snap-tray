#ifndef ANNOTATIONCONTROLLER_H
#define ANNOTATIONCONTROLLER_H

#include <QObject>
#include <QPoint>
#include <QColor>
#include <QVector>
#include <memory>

class QPainter;
class AnnotationLayer;
class PencilStroke;
class MarkerStroke;
class ArrowAnnotation;
class RectangleAnnotation;
class EllipseAnnotation;

/**
 * @brief Base annotation controller for managing drawing state.
 *
 * Handles common annotation tools: Pencil, Marker, Arrow, Rectangle.
 * Can be extended for additional tools like Mosaic, Text, StepBadge, Eraser.
 */
class AnnotationController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Common annotation tools.
     */
    enum class Tool {
        None = 0,
        Pencil,
        Marker,
        Arrow,
        Rectangle,
        Ellipse
    };

    explicit AnnotationController(QObject* parent = nullptr);
    virtual ~AnnotationController();

    /**
     * @brief Set the annotation layer to add completed annotations to.
     */
    void setAnnotationLayer(AnnotationLayer* layer);

    /**
     * @brief Set the current drawing tool.
     */
    virtual void setCurrentTool(Tool tool);

    /**
     * @brief Get the current tool.
     */
    Tool currentTool() const { return m_currentTool; }

    /**
     * @brief Set the annotation color.
     */
    void setColor(const QColor& color);

    /**
     * @brief Get the current color.
     */
    QColor color() const { return m_color; }

    /**
     * @brief Set the annotation stroke width.
     */
    void setWidth(int width);

    /**
     * @brief Get the current stroke width.
     */
    int width() const { return m_width; }

    /**
     * @brief Start drawing at the given position.
     */
    virtual void startDrawing(const QPoint& pos);

    /**
     * @brief Update the current drawing with a new position.
     */
    virtual void updateDrawing(const QPoint& pos);

    /**
     * @brief Finish the current drawing and add to layer.
     */
    virtual void finishDrawing();

    /**
     * @brief Cancel the current drawing without saving.
     */
    virtual void cancelDrawing();

    /**
     * @brief Check if currently drawing.
     */
    bool isDrawing() const { return m_isDrawing; }

    /**
     * @brief Get the draw start point.
     */
    QPoint startPoint() const { return m_startPoint; }

    /**
     * @brief Draw the in-progress annotation.
     */
    virtual void drawCurrentAnnotation(QPainter& painter) const;

    /**
     * @brief Check if the given tool is supported by this controller.
     */
    virtual bool supportsTool(Tool tool) const;

signals:
    /**
     * @brief Emitted when the drawing state changes.
     */
    void drawingStateChanged(bool isDrawing);

    /**
     * @brief Emitted when an annotation is completed.
     */
    void annotationCompleted();

protected:
    AnnotationLayer* m_annotationLayer;
    Tool m_currentTool;
    QColor m_color;
    int m_width;

    bool m_isDrawing;
    QPoint m_startPoint;
    QVector<QPoint> m_currentPath;

    // In-progress annotation objects
    std::unique_ptr<PencilStroke> m_currentPencil;
    std::unique_ptr<MarkerStroke> m_currentMarker;
    std::unique_ptr<ArrowAnnotation> m_currentArrow;
    std::unique_ptr<RectangleAnnotation> m_currentRectangle;
    std::unique_ptr<EllipseAnnotation> m_currentEllipse;
};

#endif // ANNOTATIONCONTROLLER_H
