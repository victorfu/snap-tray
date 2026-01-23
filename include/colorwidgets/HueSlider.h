#ifndef SNAPTRAY_HUE_SLIDER_H
#define SNAPTRAY_HUE_SLIDER_H

#include "colorwidgets/GradientSlider.h"

namespace snaptray {
namespace colorwidgets {

class HueSlider : public GradientSlider
{
    Q_OBJECT
    Q_PROPERTY(int saturation READ saturation WRITE setSaturation)
    Q_PROPERTY(int colorValue READ colorValue WRITE setColorValue)

public:
    explicit HueSlider(QWidget* parent = nullptr);
    explicit HueSlider(Qt::Orientation orientation, QWidget* parent = nullptr);

    int saturation() const;
    void setSaturation(int sat);

    int colorValue() const;
    void setColorValue(int val);

    QColor color() const;
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);

private:
    void updateGradient();
    void onValueChanged(int hue);

    int m_saturation = 255;
    int m_colorValue = 255;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_HUE_SLIDER_H
