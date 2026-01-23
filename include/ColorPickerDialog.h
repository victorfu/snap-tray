// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COLORPICKERDIALOG_H
#define COLORPICKERDIALOG_H

#include <QWidget>
#include <QColor>
#include <QImage>
#include <QRect>
#include <QPoint>

class QSpinBox;
class QLineEdit;
class QPushButton;

class ColorPickerDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPickerDialog(QWidget *parent = nullptr);
    ~ColorPickerDialog() override;

    void setCurrentColor(const QColor &color);
    QColor currentColor() const;

signals:
    void colorSelected(const QColor &color);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onRgbChanged();
    void onHexChanged();
    void onOkClicked();
    void onCancelClicked();

private:
    void updateFromHSV();
    void updateInputs();
    void updateSpectrumImage();
    void updateHueBarImage();
    void handleSpectrumClick(const QPoint &pos);
    void handleHueBarClick(const QPoint &pos);

    // Spectrum area (S-V plane)
    QImage m_spectrumImage;
    QRect m_spectrumRect;
    QPoint m_spectrumPos;

    // Hue bar
    QImage m_hueBarImage;
    QRect m_hueBarRect;

    // Preview areas
    QRect m_oldColorRect;
    QRect m_newColorRect;

    // Title bar for dragging
    QRect m_titleBarRect;
    QRect m_closeButtonRect;
    bool m_isDragging;
    QPoint m_dragStartPos;

    // Input fields
    QSpinBox *m_redInput;
    QSpinBox *m_greenInput;
    QSpinBox *m_blueInput;
    QLineEdit *m_hexInput;

    // Buttons
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;

    // Colors
    QColor m_currentColor;
    QColor m_oldColor;

    // HSV values (0-359, 0-255, 0-255)
    int m_hue;
    int m_saturation;
    int m_value;

    // Interaction state
    bool m_isDraggingSpectrum;
    bool m_isDraggingHue;
    bool m_updatingInputs;

    // Layout constants
    static const int DIALOG_WIDTH = 380;
    static const int DIALOG_HEIGHT = 420;
    static const int TITLE_BAR_HEIGHT = 32;
    static const int PADDING = 16;
    static const int SPECTRUM_SIZE = 200;
    static const int HUE_BAR_WIDTH = 20;
    static const int HUE_BAR_SPACING = 12;
    static const int INPUT_HEIGHT = 28;
    static const int BUTTON_HEIGHT = 32;
    static const int PREVIEW_HEIGHT = 40;
};

#endif // COLORPICKERDIALOG_H
