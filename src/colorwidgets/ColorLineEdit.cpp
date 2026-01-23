#include "colorwidgets/ColorLineEdit.h"

#include "colorwidgets/ColorUtils.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QPainter>

namespace snaptray {
namespace colorwidgets {

ColorLineEdit::ColorLineEdit(QWidget* parent) : QLineEdit(parent)
{
    setAcceptDrops(true);

    // Set up text change handling
    connect(this, &QLineEdit::textEdited, this, &ColorLineEdit::onTextEdited);

    updateFromColor();
}

void ColorLineEdit::onTextEdited(const QString& text)
{
    if (m_updating)
        return;

    QColor newColor = ColorUtils::fromString(text);
    if (newColor.isValid() && newColor != m_color) {
        m_color = newColor;
        emit colorChanged(newColor);
        emit colorEdited(newColor);
        update();
    }
}

void ColorLineEdit::updateFromColor()
{
    m_updating = true;
    setText(ColorUtils::toString(m_color, m_showAlpha));
    m_updating = false;
    update();
}

void ColorLineEdit::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);

    if (m_previewColor) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw color preview on the right side
        int size = height() - 8;
        QRect previewRect(width() - size - 4, 4, size, size);

        // Draw checkerboard for alpha
        if (m_color.alpha() < 255) {
            static const int CHECKER_SIZE = 4;
            painter.save();
            painter.setClipRect(previewRect);
            for (int y = previewRect.top(); y < previewRect.bottom(); y += CHECKER_SIZE) {
                for (int x = previewRect.left(); x < previewRect.right(); x += CHECKER_SIZE) {
                    bool light = ((x / CHECKER_SIZE) + (y / CHECKER_SIZE)) % 2 == 0;
                    painter.fillRect(x, y, CHECKER_SIZE, CHECKER_SIZE,
                                     light ? Qt::white : QColor(200, 200, 200));
                }
            }
            painter.restore();
        }

        // Draw color
        painter.fillRect(previewRect, m_color);

        // Draw border
        painter.setPen(QPen(QColor(70, 70, 70), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(previewRect);
    }
}

// Getter/Setter
QColor ColorLineEdit::color() const
{
    return m_color;
}

void ColorLineEdit::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        updateFromColor();
        emit colorChanged(color);
    }
}

bool ColorLineEdit::showAlpha() const
{
    return m_showAlpha;
}

void ColorLineEdit::setShowAlpha(bool show)
{
    if (m_showAlpha != show) {
        m_showAlpha = show;
        updateFromColor();
    }
}

bool ColorLineEdit::previewColor() const
{
    return m_previewColor;
}

void ColorLineEdit::setPreviewColor(bool preview)
{
    if (m_previewColor != preview) {
        m_previewColor = preview;
        update();
    }
}

// Drag & Drop
void ColorLineEdit::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasColor()) {
        event->acceptProposedAction();
    } else {
        QLineEdit::dragEnterEvent(event);
    }
}

void ColorLineEdit::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasColor()) {
        setColor(qvariant_cast<QColor>(event->mimeData()->colorData()));
        emit colorEdited(m_color);
        event->acceptProposedAction();
    } else {
        QLineEdit::dropEvent(event);
    }
}

}  // namespace colorwidgets
}  // namespace snaptray
