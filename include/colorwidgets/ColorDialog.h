#ifndef SNAPTRAY_COLOR_DIALOG_H
#define SNAPTRAY_COLOR_DIALOG_H

#include <QDialog>

class QSpinBox;
class QPushButton;

namespace snaptray {
namespace colorwidgets {

class ColorWheel;
class GradientSlider;
class ColorPreview;
class ColorLineEdit;

class ColorDialog : public QDialog
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(bool showAlpha READ showAlpha WRITE setShowAlpha)
    Q_PROPERTY(bool showButtons READ showButtons WRITE setShowButtons)

public:
    explicit ColorDialog(QWidget* parent = nullptr);
    explicit ColorDialog(const QColor& initial, QWidget* parent = nullptr);
    ~ColorDialog() override;

    QColor color() const;
    bool showAlpha() const;
    bool showButtons() const;

    // Static convenience method
    static QColor getColor(const QColor& initial = Qt::white, QWidget* parent = nullptr,
                           const QString& title = QString());

public slots:
    void setColor(const QColor& color);
    void setShowAlpha(bool show);
    void setShowButtons(bool show);

signals:
    void colorChanged(const QColor& color);
    void colorSelected(const QColor& color);  // Emitted when OK is pressed
    void colorApplied(const QColor& color);   // Emitted when Apply is pressed

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setupUi();
    void connectSignals();
    void updateSliders();
    void updateSpinBoxes();
    void updateHexEdit();
    void updatePreview();

    void onWheelColorChanged(const QColor& color);
    void onAlphaChanged(int alpha);
    void onHsvSliderChanged();
    void onRgbSliderChanged();
    void onHsvSpinChanged();
    void onRgbSpinChanged();
    void onResetClicked();
    void onApplyClicked();
    void onOkClicked();
    void onCancelClicked();
    void updateSliderGradients();

    // Widgets
    ColorWheel* m_wheel = nullptr;
    ColorPreview* m_preview = nullptr;
    ColorLineEdit* m_hexEdit = nullptr;

    // HSV sliders and spinboxes
    GradientSlider* m_hueSlider = nullptr;
    GradientSlider* m_satSlider = nullptr;
    GradientSlider* m_valSlider = nullptr;
    QSpinBox* m_hueSpin = nullptr;
    QSpinBox* m_satSpin = nullptr;
    QSpinBox* m_valSpin = nullptr;

    // RGB sliders and spinboxes
    GradientSlider* m_redSlider = nullptr;
    GradientSlider* m_greenSlider = nullptr;
    GradientSlider* m_blueSlider = nullptr;
    QSpinBox* m_redSpin = nullptr;
    QSpinBox* m_greenSpin = nullptr;
    QSpinBox* m_blueSpin = nullptr;

    // Alpha slider and spinbox
    GradientSlider* m_alphaSlider = nullptr;
    QSpinBox* m_alphaSpin = nullptr;

    // Buttons
    QPushButton* m_resetButton = nullptr;
    QPushButton* m_okButton = nullptr;
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_cancelButton = nullptr;

    // State
    QColor m_color = Qt::red;
    QColor m_originalColor = Qt::red;
    bool m_showAlpha = true;
    bool m_showButtons = true;
    bool m_updating = false;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_DIALOG_H
