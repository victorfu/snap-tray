#ifndef SNAPTRAY_GRADIENT_SLIDER_H
#define SNAPTRAY_GRADIENT_SLIDER_H

#include <QGradientStops>
#include <QSlider>

namespace snaptray {
namespace colorwidgets {

class GradientSlider : public QSlider
{
    Q_OBJECT
    Q_PROPERTY(QGradientStops gradient READ gradient WRITE setGradient)
    Q_PROPERTY(QColor firstColor READ firstColor WRITE setFirstColor)
    Q_PROPERTY(QColor lastColor READ lastColor WRITE setLastColor)

public:
    explicit GradientSlider(QWidget* parent = nullptr);
    explicit GradientSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    QGradientStops gradient() const;
    void setGradient(const QGradientStops& stops);

    QColor firstColor() const;
    void setFirstColor(const QColor& color);

    QColor lastColor() const;
    void setLastColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void drawCheckerboard(QPainter& painter, const QRect& rect);
    QColor handleColor() const;
    int valueFromPos(const QPoint& pos) const;
    QRect grooveRect() const;
    QRect handleRect() const;

    QGradientStops m_gradient;

    static constexpr int GROOVE_HEIGHT = 16;
    static constexpr int HANDLE_WIDTH = 10;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_GRADIENT_SLIDER_H
