#include "colorwidgets/ColorDialog.h"

#include "colorwidgets/ColorLineEdit.h"
#include "colorwidgets/ColorPreview.h"
#include "colorwidgets/ColorWheel.h"
#include "colorwidgets/GradientSlider.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace snaptray {
namespace colorwidgets {

ColorDialog::ColorDialog(QWidget* parent) : QDialog(parent)
{
    setupUi();
    connectSignals();
}

ColorDialog::ColorDialog(const QColor& initial, QWidget* parent) : QDialog(parent)
{
    setupUi();
    connectSignals();
    setColor(initial);
    m_originalColor = initial;
}

ColorDialog::~ColorDialog() = default;

void ColorDialog::setupUi()
{
    setWindowTitle(tr("Select Color"));
    setMinimumSize(520, 320);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Top area: Color wheel and sliders
    auto* topLayout = new QHBoxLayout();
    topLayout->setSpacing(16);

    // Color wheel with triangle selector
    m_wheel = new ColorWheel(this);
    m_wheel->setMinimumSize(200, 200);
    m_wheel->setFixedSize(200, 200);
    m_wheel->setWheelShape(ColorWheel::ShapeTriangle);
    topLayout->addWidget(m_wheel);

    // Right side: All sliders in a grid
    auto* slidersLayout = new QGridLayout();
    slidersLayout->setSpacing(6);
    slidersLayout->setColumnStretch(1, 1);  // Sliders stretch

    auto createSlider = [this]() {
        auto* slider = new GradientSlider(Qt::Horizontal, this);
        slider->setMinimumWidth(150);
        return slider;
    };

    auto createSpinBox = [this](int max) {
        auto* spin = new QSpinBox(this);
        spin->setRange(0, max);
        spin->setFixedWidth(55);
        return spin;
    };

    int row = 0;

    // HSV section
    // Hue (0-359)
    slidersLayout->addWidget(new QLabel(tr("Hue"), this), row, 0);
    m_hueSlider = createSlider();
    m_hueSlider->setRange(0, 359);
    slidersLayout->addWidget(m_hueSlider, row, 1);
    m_hueSpin = createSpinBox(359);
    slidersLayout->addWidget(m_hueSpin, row, 2);
    row++;

    // Saturation (0-255)
    slidersLayout->addWidget(new QLabel(tr("Saturation"), this), row, 0);
    m_satSlider = createSlider();
    m_satSlider->setRange(0, 255);
    slidersLayout->addWidget(m_satSlider, row, 1);
    m_satSpin = createSpinBox(255);
    slidersLayout->addWidget(m_satSpin, row, 2);
    row++;

    // Value (0-255)
    slidersLayout->addWidget(new QLabel(tr("Value"), this), row, 0);
    m_valSlider = createSlider();
    m_valSlider->setRange(0, 255);
    slidersLayout->addWidget(m_valSlider, row, 1);
    m_valSpin = createSpinBox(255);
    slidersLayout->addWidget(m_valSpin, row, 2);
    row++;

    // RGB section
    // Red (0-255)
    slidersLayout->addWidget(new QLabel(tr("Red"), this), row, 0);
    m_redSlider = createSlider();
    m_redSlider->setRange(0, 255);
    slidersLayout->addWidget(m_redSlider, row, 1);
    m_redSpin = createSpinBox(255);
    slidersLayout->addWidget(m_redSpin, row, 2);
    row++;

    // Green (0-255)
    slidersLayout->addWidget(new QLabel(tr("Green"), this), row, 0);
    m_greenSlider = createSlider();
    m_greenSlider->setRange(0, 255);
    slidersLayout->addWidget(m_greenSlider, row, 1);
    m_greenSpin = createSpinBox(255);
    slidersLayout->addWidget(m_greenSpin, row, 2);
    row++;

    // Blue (0-255)
    slidersLayout->addWidget(new QLabel(tr("Blue"), this), row, 0);
    m_blueSlider = createSlider();
    m_blueSlider->setRange(0, 255);
    slidersLayout->addWidget(m_blueSlider, row, 1);
    m_blueSpin = createSpinBox(255);
    slidersLayout->addWidget(m_blueSpin, row, 2);
    row++;

    // Alpha (0-255)
    slidersLayout->addWidget(new QLabel(tr("Alpha"), this), row, 0);
    m_alphaSlider = createSlider();
    m_alphaSlider->setRange(0, 255);
    slidersLayout->addWidget(m_alphaSlider, row, 1);
    m_alphaSpin = createSpinBox(255);
    slidersLayout->addWidget(m_alphaSpin, row, 2);
    row++;

    // Hex input
    slidersLayout->addWidget(new QLabel(tr("Hex"), this), row, 0);
    m_hexEdit = new ColorLineEdit(this);
    m_hexEdit->setPreviewColor(false);
    slidersLayout->addWidget(m_hexEdit, row, 1, 1, 2);

    topLayout->addLayout(slidersLayout, 1);
    mainLayout->addLayout(topLayout, 1);

    // Bottom area: Preview and buttons
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(12);

    // Color preview (old/new)
    m_preview = new ColorPreview(this);
    m_preview->setMinimumSize(80, 32);
    m_preview->setMaximumWidth(120);
    m_preview->setDisplayMode(ColorPreview::SplitColor);
    bottomLayout->addWidget(m_preview);

    bottomLayout->addStretch();

    // Buttons: Reset, OK, Apply, Cancel
    m_resetButton = new QPushButton(tr("Reset"), this);
    m_okButton = new QPushButton(tr("OK"), this);
    m_okButton->setDefault(true);
    m_applyButton = new QPushButton(tr("Apply"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);

    bottomLayout->addWidget(m_resetButton);
    bottomLayout->addWidget(m_okButton);
    bottomLayout->addWidget(m_applyButton);
    bottomLayout->addWidget(m_cancelButton);

    mainLayout->addLayout(bottomLayout);

    // Initial update
    updateSliderGradients();
    updateSpinBoxes();
    updateSliders();
    updateHexEdit();
    updatePreview();
}

void ColorDialog::connectSignals()
{
    // Color wheel
    connect(m_wheel, &ColorWheel::colorChanged, this, &ColorDialog::onWheelColorChanged);

    // HSV sliders
    connect(m_hueSlider, &GradientSlider::valueChanged, this, &ColorDialog::onHsvSliderChanged);
    connect(m_satSlider, &GradientSlider::valueChanged, this, &ColorDialog::onHsvSliderChanged);
    connect(m_valSlider, &GradientSlider::valueChanged, this, &ColorDialog::onHsvSliderChanged);

    // HSV spinboxes
    connect(m_hueSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onHsvSpinChanged);
    connect(m_satSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onHsvSpinChanged);
    connect(m_valSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onHsvSpinChanged);

    // RGB sliders
    connect(m_redSlider, &GradientSlider::valueChanged, this, &ColorDialog::onRgbSliderChanged);
    connect(m_greenSlider, &GradientSlider::valueChanged, this, &ColorDialog::onRgbSliderChanged);
    connect(m_blueSlider, &GradientSlider::valueChanged, this, &ColorDialog::onRgbSliderChanged);

    // RGB spinboxes
    connect(m_redSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onRgbSpinChanged);
    connect(m_greenSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onRgbSpinChanged);
    connect(m_blueSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ColorDialog::onRgbSpinChanged);

    // Alpha slider
    connect(m_alphaSlider, &GradientSlider::valueChanged, this, &ColorDialog::onAlphaChanged);

    // Alpha spinbox
    connect(m_alphaSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (!m_updating) {
                    m_updating = true;
                    m_color.setAlpha(value);
                    m_alphaSlider->setValue(value);
                    updateHexEdit();
                    updatePreview();
                    emit colorChanged(m_color);
                    m_updating = false;
                }
            });

    // Hex edit
    connect(m_hexEdit, &ColorLineEdit::colorEdited, this, [this](const QColor& color) {
        if (!m_updating) {
            setColor(color);
        }
    });

    // Buttons
    connect(m_resetButton, &QPushButton::clicked, this, &ColorDialog::onResetClicked);
    connect(m_okButton, &QPushButton::clicked, this, &ColorDialog::onOkClicked);
    connect(m_applyButton, &QPushButton::clicked, this, &ColorDialog::onApplyClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &ColorDialog::onCancelClicked);
}

void ColorDialog::onWheelColorChanged(const QColor& color)
{
    if (m_updating)
        return;

    m_updating = true;
    m_color.setRgb(color.red(), color.green(), color.blue(), m_color.alpha());
    updateSpinBoxes();
    updateSliders();
    updateSliderGradients();
    updateHexEdit();
    updatePreview();
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onAlphaChanged(int alpha)
{
    if (m_updating)
        return;
    if (!m_alphaSpin)
        return;

    m_updating = true;
    m_color.setAlpha(alpha);
    m_alphaSpin->setValue(alpha);
    updateHexEdit();
    updatePreview();
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onHsvSliderChanged()
{
    if (m_updating)
        return;
    if (!m_hueSlider || !m_satSlider || !m_valSlider || !m_wheel)
        return;

    m_updating = true;
    m_color = QColor::fromHsv(m_hueSlider->value(), m_satSlider->value(), m_valSlider->value(),
                               m_color.alpha());
    m_wheel->setColor(m_color);
    updateSpinBoxes();
    updateSliderGradients();
    updateHexEdit();
    updatePreview();
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onRgbSliderChanged()
{
    if (m_updating)
        return;
    if (!m_redSlider || !m_greenSlider || !m_blueSlider || !m_wheel)
        return;

    m_updating = true;
    m_color.setRgb(m_redSlider->value(), m_greenSlider->value(), m_blueSlider->value(),
                   m_color.alpha());
    m_wheel->setColor(m_color);
    updateSpinBoxes();
    updateSliderGradients();
    updateHexEdit();
    updatePreview();
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onHsvSpinChanged()
{
    if (m_updating)
        return;
    if (!m_hueSpin || !m_satSpin || !m_valSpin || !m_wheel ||
        !m_redSpin || !m_greenSpin || !m_blueSpin)
        return;

    m_updating = true;
    m_color = QColor::fromHsv(m_hueSpin->value(), m_satSpin->value(), m_valSpin->value(),
                               m_color.alpha());
    m_wheel->setColor(m_color);
    updateSliders();
    updateSliderGradients();
    updateHexEdit();
    updatePreview();
    // Update RGB spinboxes
    m_redSpin->setValue(m_color.red());
    m_greenSpin->setValue(m_color.green());
    m_blueSpin->setValue(m_color.blue());
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onRgbSpinChanged()
{
    if (m_updating)
        return;
    if (!m_redSpin || !m_greenSpin || !m_blueSpin || !m_wheel ||
        !m_hueSpin || !m_satSpin || !m_valSpin)
        return;

    m_updating = true;
    m_color.setRgb(m_redSpin->value(), m_greenSpin->value(), m_blueSpin->value(), m_color.alpha());
    m_wheel->setColor(m_color);
    updateSliders();
    updateSliderGradients();
    updateHexEdit();
    updatePreview();
    // Update HSV spinboxes
    m_hueSpin->setValue(m_color.hsvHue() >= 0 ? m_color.hsvHue() : 0);
    m_satSpin->setValue(m_color.hsvSaturation());
    m_valSpin->setValue(m_color.value());
    emit colorChanged(m_color);
    m_updating = false;
}

void ColorDialog::onResetClicked()
{
    setColor(m_originalColor);
}

void ColorDialog::onApplyClicked()
{
    emit colorApplied(m_color);
}

void ColorDialog::onOkClicked()
{
    emit colorSelected(m_color);
    accept();
}

void ColorDialog::onCancelClicked()
{
    m_color = m_originalColor;
    reject();
}

void ColorDialog::updateSliders()
{
    if (!m_hueSlider || !m_satSlider || !m_valSlider ||
        !m_redSlider || !m_greenSlider || !m_blueSlider || !m_alphaSlider) {
        return;
    }

    // Update HSV sliders
    int hue = m_color.hsvHue();
    if (hue < 0)
        hue = 0;
    m_hueSlider->setValue(hue);
    m_satSlider->setValue(m_color.hsvSaturation());
    m_valSlider->setValue(m_color.value());

    // Update RGB sliders
    m_redSlider->setValue(m_color.red());
    m_greenSlider->setValue(m_color.green());
    m_blueSlider->setValue(m_color.blue());

    // Update Alpha slider
    m_alphaSlider->setValue(m_color.alpha());
}

void ColorDialog::updateSliderGradients()
{
    if (!m_hueSlider || !m_satSlider || !m_valSlider ||
        !m_redSlider || !m_greenSlider || !m_blueSlider || !m_alphaSlider) {
        return;
    }

    QGradientStops stops;

    // Hue slider: rainbow gradient
    stops.clear();
    for (int i = 0; i <= 6; ++i) {
        double pos = i / 6.0;
        int hue = static_cast<int>(pos * 360) % 360;
        stops << qMakePair(pos, QColor::fromHsv(hue, 255, 255));
    }
    m_hueSlider->setGradient(stops);

    // Saturation slider: gray to current hue at full saturation
    int hue = m_color.hsvHue();
    if (hue < 0)
        hue = 0;
    int val = m_color.value();
    stops.clear();
    stops << qMakePair(0.0, QColor::fromHsv(hue, 0, val));
    stops << qMakePair(1.0, QColor::fromHsv(hue, 255, val));
    m_satSlider->setGradient(stops);

    // Value slider: black to current hue at full value
    int sat = m_color.hsvSaturation();
    stops.clear();
    stops << qMakePair(0.0, QColor::fromHsv(hue, sat, 0));
    stops << qMakePair(1.0, QColor::fromHsv(hue, sat, 255));
    m_valSlider->setGradient(stops);

    // Red slider: current color with varying red
    stops.clear();
    stops << qMakePair(0.0, QColor(0, m_color.green(), m_color.blue()));
    stops << qMakePair(1.0, QColor(255, m_color.green(), m_color.blue()));
    m_redSlider->setGradient(stops);

    // Green slider: current color with varying green
    stops.clear();
    stops << qMakePair(0.0, QColor(m_color.red(), 0, m_color.blue()));
    stops << qMakePair(1.0, QColor(m_color.red(), 255, m_color.blue()));
    m_greenSlider->setGradient(stops);

    // Blue slider: current color with varying blue
    stops.clear();
    stops << qMakePair(0.0, QColor(m_color.red(), m_color.green(), 0));
    stops << qMakePair(1.0, QColor(m_color.red(), m_color.green(), 255));
    m_blueSlider->setGradient(stops);

    // Alpha slider: transparent to opaque
    QColor transparent = m_color;
    transparent.setAlpha(0);
    QColor opaque = m_color;
    opaque.setAlpha(255);
    stops.clear();
    stops << qMakePair(0.0, transparent);
    stops << qMakePair(1.0, opaque);
    m_alphaSlider->setGradient(stops);
}

void ColorDialog::updateSpinBoxes()
{
    if (!m_hueSpin || !m_satSpin || !m_valSpin ||
        !m_redSpin || !m_greenSpin || !m_blueSpin || !m_alphaSpin) {
        return;
    }

    // HSV spinboxes
    int hue = m_color.hsvHue();
    if (hue < 0)
        hue = 0;
    m_hueSpin->setValue(hue);
    m_satSpin->setValue(m_color.hsvSaturation());
    m_valSpin->setValue(m_color.value());

    // RGB spinboxes
    m_redSpin->setValue(m_color.red());
    m_greenSpin->setValue(m_color.green());
    m_blueSpin->setValue(m_color.blue());

    // Alpha spinbox
    m_alphaSpin->setValue(m_color.alpha());
}

void ColorDialog::updateHexEdit()
{
    if (!m_hexEdit) {
        return;
    }
    m_hexEdit->setColor(m_color);
    m_hexEdit->setShowAlpha(m_showAlpha);
}

void ColorDialog::updatePreview()
{
    if (!m_preview) {
        return;
    }
    m_preview->setColor(m_color);
    m_preview->setComparisonColor(m_originalColor);
}

// Properties
QColor ColorDialog::color() const
{
    return m_color;
}

void ColorDialog::setColor(const QColor& color)
{
    if (m_color != color) {
        m_updating = true;
        m_color = color;
        if (m_wheel) {
            m_wheel->setColor(color);
        }
        updateSliders();
        updateSliderGradients();
        updateSpinBoxes();
        updateHexEdit();
        updatePreview();
        emit colorChanged(color);
        m_updating = false;
    }
}

bool ColorDialog::showAlpha() const
{
    return m_showAlpha;
}

void ColorDialog::setShowAlpha(bool show)
{
    if (m_showAlpha != show) {
        m_showAlpha = show;
        if (m_alphaSlider) {
            m_alphaSlider->setVisible(show);
        }
        if (m_alphaSpin) {
            m_alphaSpin->setVisible(show);
        }
        updateHexEdit();
    }
}

bool ColorDialog::showButtons() const
{
    return m_showButtons;
}

void ColorDialog::setShowButtons(bool show)
{
    if (m_showButtons != show) {
        m_showButtons = show;
        if (m_resetButton) {
            m_resetButton->setVisible(show);
        }
        if (m_okButton) {
            m_okButton->setVisible(show);
        }
        if (m_applyButton) {
            m_applyButton->setVisible(show);
        }
        if (m_cancelButton) {
            m_cancelButton->setVisible(show);
        }
    }
}

void ColorDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_originalColor = m_color;
    updatePreview();
}

void ColorDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        onCancelClicked();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onOkClicked();
    } else {
        QDialog::keyPressEvent(event);
    }
}

// Static method
QColor ColorDialog::getColor(const QColor& initial, QWidget* parent, const QString& title)
{
    ColorDialog dialog(initial, parent);
    if (!title.isEmpty()) {
        dialog.setWindowTitle(title);
    }

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.color();
    }
    return initial;
}

}  // namespace colorwidgets
}  // namespace snaptray
