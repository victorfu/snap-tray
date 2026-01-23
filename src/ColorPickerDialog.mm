// Copyright (c) 2025 Victor Fu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ColorPickerDialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRegularExpressionValidator>
#include <QWindow>

#ifdef Q_OS_MACOS
#import <AppKit/AppKit.h>
#endif

ColorPickerDialog::ColorPickerDialog(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_isDragging(false)
    , m_isDraggingSpectrum(false)
    , m_isDraggingHue(false)
    , m_updatingInputs(false)
    , m_hue(0)
    , m_saturation(255)
    , m_value(255)
    , m_currentColor(Qt::red)
    , m_oldColor(Qt::red)
{
    // Enable transparent background for rounded corners
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(DIALOG_WIDTH + 10, DIALOG_HEIGHT + 10);  // Extra space for shadow
    setAttribute(Qt::WA_DeleteOnClose, false);

    // Shadow offset for all positions
    const int shadowOffset = 5;

    // Calculate layout positions
    int y = shadowOffset + TITLE_BAR_HEIGHT + PADDING;

    // Spectrum rect (left side)
    m_spectrumRect = QRect(shadowOffset + PADDING, y, SPECTRUM_SIZE, SPECTRUM_SIZE);

    // Hue bar (right of spectrum)
    m_hueBarRect = QRect(shadowOffset + PADDING + SPECTRUM_SIZE + HUE_BAR_SPACING, y, HUE_BAR_WIDTH, SPECTRUM_SIZE);

    // Title bar and close button
    m_titleBarRect = QRect(shadowOffset, shadowOffset, DIALOG_WIDTH, TITLE_BAR_HEIGHT);
    m_closeButtonRect = QRect(shadowOffset + DIALOG_WIDTH - 32, shadowOffset + 4, 24, 24);

    // Input area (right of hue bar)
    int inputX = m_hueBarRect.right() + PADDING;
    int inputWidth = shadowOffset + DIALOG_WIDTH - inputX - PADDING;
    int inputY = y;

    // Create input widgets
    m_redInput = new QSpinBox(this);
    m_redInput->setRange(0, 255);
    m_redInput->setGeometry(inputX + 24, inputY, inputWidth - 24, INPUT_HEIGHT);
    connect(m_redInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &ColorPickerDialog::onRgbChanged);

    inputY += INPUT_HEIGHT + 8;
    m_greenInput = new QSpinBox(this);
    m_greenInput->setRange(0, 255);
    m_greenInput->setGeometry(inputX + 24, inputY, inputWidth - 24, INPUT_HEIGHT);
    connect(m_greenInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &ColorPickerDialog::onRgbChanged);

    inputY += INPUT_HEIGHT + 8;
    m_blueInput = new QSpinBox(this);
    m_blueInput->setRange(0, 255);
    m_blueInput->setGeometry(inputX + 24, inputY, inputWidth - 24, INPUT_HEIGHT);
    connect(m_blueInput, QOverload<int>::of(&QSpinBox::valueChanged), this, &ColorPickerDialog::onRgbChanged);

    inputY += INPUT_HEIGHT + 16;
    m_hexInput = new QLineEdit(this);
    m_hexInput->setGeometry(inputX, inputY, inputWidth, INPUT_HEIGHT);
    m_hexInput->setMaxLength(7);
    QRegularExpression hexRegex("^#[0-9A-Fa-f]{0,6}$");
    m_hexInput->setValidator(new QRegularExpressionValidator(hexRegex, this));
    connect(m_hexInput, &QLineEdit::textEdited, this, &ColorPickerDialog::onHexChanged);

    // Preview areas
    y = m_spectrumRect.bottom() + PADDING;
    int previewWidth = (SPECTRUM_SIZE + HUE_BAR_SPACING + HUE_BAR_WIDTH) / 2 - 4;
    m_oldColorRect = QRect(shadowOffset + PADDING, y, previewWidth, PREVIEW_HEIGHT);
    m_newColorRect = QRect(shadowOffset + PADDING + previewWidth + 8, y, previewWidth, PREVIEW_HEIGHT);

    // Buttons
    y = m_oldColorRect.bottom() + PADDING;
    int buttonWidth = 80;
    int buttonSpacing = 12;

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setGeometry(shadowOffset + DIALOG_WIDTH - PADDING - buttonWidth * 2 - buttonSpacing, y, buttonWidth, BUTTON_HEIGHT);
    connect(m_cancelButton, &QPushButton::clicked, this, &ColorPickerDialog::onCancelClicked);

    m_okButton = new QPushButton(tr("OK"), this);
    m_okButton->setGeometry(shadowOffset + DIALOG_WIDTH - PADDING - buttonWidth, y, buttonWidth, BUTTON_HEIGHT);
    connect(m_okButton, &QPushButton::clicked, this, &ColorPickerDialog::onOkClicked);

    // Apply dark theme stylesheet
    QString styleSheet = R"(
        QSpinBox, QLineEdit {
            background-color: rgb(55, 55, 55);
            color: white;
            border: 1px solid rgb(70, 70, 70);
            border-radius: 4px;
            padding: 2px 6px;
            selection-background-color: rgb(0, 120, 200);
        }
        QSpinBox:focus, QLineEdit:focus {
            border: 1px solid rgb(0, 120, 200);
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: rgb(60, 60, 60);
            border: none;
            width: 16px;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: rgb(80, 80, 80);
        }
        QSpinBox::up-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-bottom: 4px solid white;
            width: 0; height: 0;
        }
        QSpinBox::down-arrow {
            image: none;
            border-left: 4px solid transparent;
            border-right: 4px solid transparent;
            border-top: 4px solid white;
            width: 0; height: 0;
        }
        QPushButton {
            background-color: rgb(60, 60, 60);
            color: white;
            border: 1px solid rgb(70, 70, 70);
            border-radius: 4px;
            padding: 4px 16px;
        }
        QPushButton:hover {
            background-color: rgb(80, 80, 80);
        }
        QPushButton:pressed {
            background-color: rgb(50, 50, 50);
        }
    )";
    setStyleSheet(styleSheet);

    // Style OK button with accent color
    m_okButton->setStyleSheet(R"(
        QPushButton {
            background-color: rgb(0, 120, 200);
            color: white;
            border: none;
            border-radius: 4px;
            padding: 4px 16px;
        }
        QPushButton:hover {
            background-color: rgb(0, 140, 220);
        }
        QPushButton:pressed {
            background-color: rgb(0, 100, 180);
        }
    )");

    // Initialize images
    updateHueBarImage();
    updateSpectrumImage();
    updateInputs();
}

ColorPickerDialog::~ColorPickerDialog() = default;

void ColorPickerDialog::setCurrentColor(const QColor &color)
{
    m_currentColor = color;
    m_oldColor = color;

    // Convert to HSV
    m_hue = color.hsvHue();
    if (m_hue < 0) m_hue = 0;
    m_saturation = color.hsvSaturation();
    m_value = color.value();

    // Update spectrum position
    m_spectrumPos = QPoint(
        m_saturation * SPECTRUM_SIZE / 255,
        (255 - m_value) * SPECTRUM_SIZE / 255
    );

    updateSpectrumImage();
    updateInputs();
    update();
}

QColor ColorPickerDialog::currentColor() const
{
    return m_currentColor;
}

void ColorPickerDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

#ifdef Q_OS_MACOS
    // Set window level above RegionSelector/ScreenCanvas (which is at kCGScreenSaverWindowLevel = 1000)
    NSView* view = reinterpret_cast<NSView*>(winId());
    if (view) {
        NSWindow* window = [view window];
        [window setLevel:kCGScreenSaverWindowLevel + 1];
    }
#endif
}

void ColorPickerDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int shadowOffset = 5;
    const int cornerRadius = 12;
    QRect dialogRect(shadowOffset, shadowOffset, DIALOG_WIDTH, DIALOG_HEIGHT);

    // Draw shadow layers for depth effect
    for (int i = 4; i >= 1; --i) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 15 * i));
        painter.drawRoundedRect(dialogRect.adjusted(-i, -i, i, i), cornerRadius + i, cornerRadius + i);
    }

    // Draw main background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40));
    painter.drawRoundedRect(dialogRect, cornerRadius, cornerRadius);

    // Draw subtle border
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(dialogRect, cornerRadius, cornerRadius);

    // Draw title bar with gradient
    QPainterPath titlePath;
    titlePath.addRoundedRect(QRectF(dialogRect.left(), dialogRect.top(), DIALOG_WIDTH, TITLE_BAR_HEIGHT), cornerRadius, cornerRadius);
    // Clip bottom corners
    titlePath.addRect(QRectF(dialogRect.left(), dialogRect.top() + TITLE_BAR_HEIGHT - cornerRadius, DIALOG_WIDTH, cornerRadius));

    QLinearGradient titleGradient(dialogRect.left(), dialogRect.top(), dialogRect.left(), dialogRect.top() + TITLE_BAR_HEIGHT);
    titleGradient.setColorAt(0, QColor(55, 55, 55));
    titleGradient.setColorAt(1, QColor(45, 45, 45));
    painter.setPen(Qt::NoPen);
    painter.setBrush(titleGradient);
    painter.setClipPath(titlePath);
    painter.drawRect(QRect(dialogRect.left(), dialogRect.top(), DIALOG_WIDTH, TITLE_BAR_HEIGHT));
    painter.setClipping(false);

    // Draw title text
    painter.setPen(Qt::white);
    QFont titleFont = font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    QRect titleTextRect(dialogRect.left(), dialogRect.top(), DIALOG_WIDTH, TITLE_BAR_HEIGHT);
    painter.drawText(titleTextRect, Qt::AlignCenter, tr("Select Color"));

    // Draw close button
    QRect closeRect(dialogRect.right() - 32, dialogRect.top() + 4, 24, 24);
    painter.setPen(QPen(QColor(150, 150, 150), 2));
    int cx = closeRect.center().x();
    int cy = closeRect.center().y();
    painter.drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
    painter.drawLine(cx + 5, cy - 5, cx - 5, cy + 5);

    // Draw spectrum
    painter.drawImage(m_spectrumRect, m_spectrumImage);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_spectrumRect);

    // Draw spectrum cursor
    int cursorX = m_spectrumRect.left() + m_spectrumPos.x();
    int cursorY = m_spectrumRect.top() + m_spectrumPos.y();
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPoint(cursorX, cursorY), 6, 6);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(QPoint(cursorX, cursorY), 7, 7);

    // Draw hue bar
    painter.drawImage(m_hueBarRect, m_hueBarImage);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_hueBarRect);

    // Draw hue cursor
    int hueY = m_hueBarRect.top() + m_hue * SPECTRUM_SIZE / 359;
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    QPolygon leftArrow, rightArrow;
    leftArrow << QPoint(m_hueBarRect.left() - 6, hueY)
              << QPoint(m_hueBarRect.left() - 1, hueY - 5)
              << QPoint(m_hueBarRect.left() - 1, hueY + 5);
    rightArrow << QPoint(m_hueBarRect.right() + 6, hueY)
               << QPoint(m_hueBarRect.right() + 1, hueY - 5)
               << QPoint(m_hueBarRect.right() + 1, hueY + 5);
    painter.drawPolygon(leftArrow);
    painter.drawPolygon(rightArrow);

    // Draw RGB labels
    painter.setPen(Qt::white);
    QFont labelFont = font();
    labelFont.setPointSize(11);
    painter.setFont(labelFont);

    int labelX = m_hueBarRect.right() + PADDING;
    int labelY = m_spectrumRect.top();
    painter.drawText(labelX, labelY + INPUT_HEIGHT / 2 + 4, "R");
    painter.drawText(labelX, labelY + INPUT_HEIGHT + 8 + INPUT_HEIGHT / 2 + 4, "G");
    painter.drawText(labelX, labelY + (INPUT_HEIGHT + 8) * 2 + INPUT_HEIGHT / 2 + 4, "B");

    // Draw preview labels
    painter.setPen(QColor(180, 180, 180));
    labelFont.setPointSize(10);
    painter.setFont(labelFont);
    painter.drawText(m_oldColorRect.left(), m_oldColorRect.top() - 4, tr("Old"));
    painter.drawText(m_newColorRect.left(), m_newColorRect.top() - 4, tr("New"));

    // Draw old color preview
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(m_oldColor);
    painter.drawRoundedRect(m_oldColorRect, 4, 4);

    // Draw new color preview
    painter.setBrush(m_currentColor);
    painter.drawRoundedRect(m_newColorRect, 4, 4);
}

void ColorPickerDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check close button
        if (m_closeButtonRect.contains(event->pos())) {
            onCancelClicked();
            return;
        }

        // Check title bar for dragging
        if (m_titleBarRect.contains(event->pos()) && !m_closeButtonRect.contains(event->pos())) {
            m_isDragging = true;
            m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            return;
        }

        // Check spectrum area
        if (m_spectrumRect.contains(event->pos())) {
            m_isDraggingSpectrum = true;
            handleSpectrumClick(event->pos());
            return;
        }

        // Check hue bar
        if (m_hueBarRect.adjusted(-6, 0, 6, 0).contains(event->pos())) {
            m_isDraggingHue = true;
            handleHueBarClick(event->pos());
            return;
        }

        // Check old color preview (revert to old color)
        if (m_oldColorRect.contains(event->pos())) {
            setCurrentColor(m_oldColor);
            return;
        }
    }
}

void ColorPickerDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        return;
    }

    if (m_isDraggingSpectrum) {
        handleSpectrumClick(event->pos());
        return;
    }

    if (m_isDraggingHue) {
        handleHueBarClick(event->pos());
        return;
    }
}

void ColorPickerDialog::mouseReleaseEvent(QMouseEvent *)
{
    m_isDragging = false;
    m_isDraggingSpectrum = false;
    m_isDraggingHue = false;
}

void ColorPickerDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        onCancelClicked();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onOkClicked();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void ColorPickerDialog::onRgbChanged()
{
    if (m_updatingInputs) return;

    m_currentColor = QColor(m_redInput->value(), m_greenInput->value(), m_blueInput->value());

    // Update HSV values
    m_hue = m_currentColor.hsvHue();
    if (m_hue < 0) m_hue = 0;
    m_saturation = m_currentColor.hsvSaturation();
    m_value = m_currentColor.value();

    // Update spectrum position
    m_spectrumPos = QPoint(
        m_saturation * SPECTRUM_SIZE / 255,
        (255 - m_value) * SPECTRUM_SIZE / 255
    );

    updateSpectrumImage();

    // Update hex input
    m_updatingInputs = true;
    m_hexInput->setText(m_currentColor.name().toUpper());
    m_updatingInputs = false;

    update();
}

void ColorPickerDialog::onHexChanged()
{
    if (m_updatingInputs) return;

    QString hex = m_hexInput->text();
    if (!hex.startsWith('#')) {
        hex = '#' + hex;
    }

    if (hex.length() == 7) {
        QColor color(hex);
        if (color.isValid()) {
            m_currentColor = color;

            // Update HSV values
            m_hue = m_currentColor.hsvHue();
            if (m_hue < 0) m_hue = 0;
            m_saturation = m_currentColor.hsvSaturation();
            m_value = m_currentColor.value();

            // Update spectrum position
            m_spectrumPos = QPoint(
                m_saturation * SPECTRUM_SIZE / 255,
                (255 - m_value) * SPECTRUM_SIZE / 255
            );

            updateSpectrumImage();

            // Update RGB inputs
            m_updatingInputs = true;
            m_redInput->setValue(m_currentColor.red());
            m_greenInput->setValue(m_currentColor.green());
            m_blueInput->setValue(m_currentColor.blue());
            m_updatingInputs = false;

            update();
        }
    }
}

void ColorPickerDialog::onOkClicked()
{
    emit colorSelected(m_currentColor);
    hide();
}

void ColorPickerDialog::onCancelClicked()
{
    emit cancelled();
    hide();
}

void ColorPickerDialog::updateFromHSV()
{
    m_currentColor = QColor::fromHsv(m_hue, m_saturation, m_value);
    updateInputs();
    update();
}

void ColorPickerDialog::updateInputs()
{
    m_updatingInputs = true;
    m_redInput->setValue(m_currentColor.red());
    m_greenInput->setValue(m_currentColor.green());
    m_blueInput->setValue(m_currentColor.blue());
    m_hexInput->setText(m_currentColor.name().toUpper());
    m_updatingInputs = false;
}

void ColorPickerDialog::updateSpectrumImage()
{
    m_spectrumImage = QImage(SPECTRUM_SIZE, SPECTRUM_SIZE, QImage::Format_RGB32);

    for (int y = 0; y < SPECTRUM_SIZE; ++y) {
        int value = 255 - y * 255 / (SPECTRUM_SIZE - 1);
        QRgb *line = reinterpret_cast<QRgb*>(m_spectrumImage.scanLine(y));
        for (int x = 0; x < SPECTRUM_SIZE; ++x) {
            int saturation = x * 255 / (SPECTRUM_SIZE - 1);
            QColor color = QColor::fromHsv(m_hue, saturation, value);
            line[x] = color.rgb();
        }
    }
}

void ColorPickerDialog::updateHueBarImage()
{
    m_hueBarImage = QImage(HUE_BAR_WIDTH, SPECTRUM_SIZE, QImage::Format_RGB32);

    for (int y = 0; y < SPECTRUM_SIZE; ++y) {
        int hue = y * 359 / (SPECTRUM_SIZE - 1);
        QColor color = QColor::fromHsv(hue, 255, 255);
        QRgb *line = reinterpret_cast<QRgb*>(m_hueBarImage.scanLine(y));
        for (int x = 0; x < HUE_BAR_WIDTH; ++x) {
            line[x] = color.rgb();
        }
    }
}

void ColorPickerDialog::handleSpectrumClick(const QPoint &pos)
{
    int x = qBound(0, pos.x() - m_spectrumRect.left(), SPECTRUM_SIZE - 1);
    int y = qBound(0, pos.y() - m_spectrumRect.top(), SPECTRUM_SIZE - 1);

    m_spectrumPos = QPoint(x, y);
    m_saturation = x * 255 / (SPECTRUM_SIZE - 1);
    m_value = 255 - y * 255 / (SPECTRUM_SIZE - 1);

    updateFromHSV();
}

void ColorPickerDialog::handleHueBarClick(const QPoint &pos)
{
    int y = qBound(0, pos.y() - m_hueBarRect.top(), SPECTRUM_SIZE - 1);
    m_hue = y * 359 / (SPECTRUM_SIZE - 1);

    updateSpectrumImage();
    updateFromHSV();
}
