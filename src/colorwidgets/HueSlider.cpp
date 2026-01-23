#include "colorwidgets/HueSlider.h"

namespace snaptray {
namespace colorwidgets {

HueSlider::HueSlider(QWidget* parent) : HueSlider(Qt::Horizontal, parent) {}

HueSlider::HueSlider(Qt::Orientation orientation, QWidget* parent)
    : GradientSlider(orientation, parent)
{
    setRange(0, 359);
    updateGradient();

    connect(this, &QSlider::valueChanged, this, &HueSlider::onValueChanged);
}

void HueSlider::updateGradient()
{
    QGradientStops stops;

    // Rainbow gradient: R → Y → G → C → B → M → R
    for (int h = 0; h <= 360; h += 60) {
        QColor c = QColor::fromHsv(h % 360, m_saturation, m_colorValue);
        stops << qMakePair(h / 360.0, c);
    }

    setGradient(stops);
}

void HueSlider::onValueChanged(int hue)
{
    emit colorChanged(QColor::fromHsv(hue, m_saturation, m_colorValue));
}

QColor HueSlider::color() const
{
    return QColor::fromHsv(value(), m_saturation, m_colorValue);
}

void HueSlider::setColor(const QColor& color)
{
    m_saturation = color.hsvSaturation();
    m_colorValue = color.value();
    setValue(color.hsvHue());
    updateGradient();
}

int HueSlider::saturation() const
{
    return m_saturation;
}

void HueSlider::setSaturation(int sat)
{
    m_saturation = qBound(0, sat, 255);
    updateGradient();
}

int HueSlider::colorValue() const
{
    return m_colorValue;
}

void HueSlider::setColorValue(int val)
{
    m_colorValue = qBound(0, val, 255);
    updateGradient();
}

}  // namespace colorwidgets
}  // namespace snaptray
