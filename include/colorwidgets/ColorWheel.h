#ifndef SNAPTRAY_COLOR_WHEEL_H
#define SNAPTRAY_COLOR_WHEEL_H

#include <QImage>
#include <QLineF>
#include <QPixmap>
#include <QWidget>

namespace snaptray {
namespace colorwidgets {

class ColorWheel : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(int hue READ hue WRITE setHue NOTIFY hueChanged)
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation NOTIFY saturationChanged)
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(ShapeEnum wheelShape READ wheelShape WRITE setWheelShape)
    Q_PROPERTY(ColorSpaceEnum colorSpace READ colorSpace WRITE setColorSpace)
    Q_PROPERTY(bool wheelRotating READ wheelRotating WRITE setWheelRotating)

public:
    enum ShapeEnum {
        ShapeTriangle,  // Triangle selector (like GIMP)
        ShapeSquare     // Square selector (like Photoshop)
    };
    Q_ENUM(ShapeEnum)

    enum ColorSpaceEnum {
        ColorHSV,  // Hue-Saturation-Value
        ColorHSL,  // Hue-Saturation-Lightness
        ColorLCH   // Lightness-Chroma-Hue (perceptually uniform)
    };
    Q_ENUM(ColorSpaceEnum)

    explicit ColorWheel(QWidget* parent = nullptr);
    ~ColorWheel() override;

    QColor color() const;
    int hue() const;
    int saturation() const;
    int value() const;

    ShapeEnum wheelShape() const;
    ColorSpaceEnum colorSpace() const;
    bool wheelRotating() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setColor(const QColor& color);
    void setHue(int hue);
    void setSaturation(int saturation);
    void setValue(int value);
    void setWheelShape(ShapeEnum shape);
    void setColorSpace(ColorSpaceEnum space);
    void setWheelRotating(bool rotating);

signals:
    void colorChanged(const QColor& color);
    void colorSelected(const QColor& color);  // Emitted on mouse release
    void hueChanged(int hue);
    void saturationChanged(int saturation);
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum DragMode { DragNone, DragHue, DragSquare };

    // Rendering
    void renderWheel();
    void renderSquare();
    void renderTriangle();

    // Geometry calculations
    qreal outerRadius() const;
    qreal innerRadius() const;
    qreal squareSize() const;
    qreal triangleHeight() const;
    qreal triangleSide() const;
    QSizeF selectorSize() const;
    QPointF selectorImageOffset() const;
    qreal selectorImageAngle() const;

    // Input handling
    QLineF lineToPoint(const QPoint& p) const;

    // Data members
    int m_hue = 0;           // 0-359
    int m_saturation = 255;  // 0-255
    int m_value = 255;       // 0-255

    ShapeEnum m_shape = ShapeTriangle;
    ColorSpaceEnum m_colorSpace = ColorHSV;
    bool m_rotating = true;

    DragMode m_dragMode = DragNone;

    QPixmap m_wheelImage;
    QImage m_selectorImage;
    bool m_wheelDirty = true;
    bool m_selectorDirty = true;

    // Layout constants
    static constexpr int WHEEL_THICKNESS = 25;  // Color wheel ring width
    static constexpr int SELECTOR_MARGIN = 5;   // Gap between selector and wheel
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_WHEEL_H
