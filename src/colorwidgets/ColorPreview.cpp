#include "colorwidgets/ColorPreview.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

namespace snaptray {
namespace colorwidgets {

ColorPreview::ColorPreview(QWidget* parent) : QWidget(parent)
{
    setAcceptDrops(true);
    setMinimumSize(40, 40);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

QSize ColorPreview::sizeHint() const
{
    return QSize(60, 40);
}

void ColorPreview::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);

    switch (m_displayMode) {
        case NoAlpha:
            painter.fillRect(r, m_color.rgb());  // Ignore alpha
            break;

        case AllAlpha:
            drawCheckerboard(painter, r);
            painter.fillRect(r, m_color);
            break;

        case SplitAlpha: {
            QRect left(r.left(), r.top(), r.width() / 2, r.height());
            QRect right(r.left() + r.width() / 2, r.top(), r.width() / 2, r.height());

            painter.fillRect(left, m_color.rgb());  // Opaque
            drawCheckerboard(painter, right);
            painter.fillRect(right, m_color);  // With transparency
            break;
        }

        case SplitColor: {
            QRect left(r.left(), r.top(), r.width() / 2, r.height());
            QRect right(r.left() + r.width() / 2, r.top(), r.width() / 2, r.height());

            painter.fillRect(left, m_color);
            painter.fillRect(right, m_comparisonColor);
            break;
        }

        case SplitColorReverse: {
            QRect left(r.left(), r.top(), r.width() / 2, r.height());
            QRect right(r.left() + r.width() / 2, r.top(), r.width() / 2, r.height());

            painter.fillRect(left, m_comparisonColor);
            painter.fillRect(right, m_color);
            break;
        }
    }

    // Border
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(r, 4, 4);
}

void ColorPreview::drawCheckerboard(QPainter& painter, const QRect& rect)
{
    static const int SIZE = 8;
    painter.save();
    painter.setClipRect(rect);

    for (int y = rect.top(); y < rect.bottom(); y += SIZE) {
        for (int x = rect.left(); x < rect.right(); x += SIZE) {
            bool light = ((x / SIZE) + (y / SIZE)) % 2 == 0;
            painter.fillRect(x, y, SIZE, SIZE, light ? Qt::white : QColor(204, 204, 204));
        }
    }

    painter.restore();
}

void ColorPreview::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

// Getter/Setter
QColor ColorPreview::color() const
{
    return m_color;
}

void ColorPreview::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        emit colorChanged(color);
        update();
    }
}

QColor ColorPreview::comparisonColor() const
{
    return m_comparisonColor;
}

void ColorPreview::setComparisonColor(const QColor& color)
{
    m_comparisonColor = color;
    update();
}

ColorPreview::DisplayMode ColorPreview::displayMode() const
{
    return m_displayMode;
}

void ColorPreview::setDisplayMode(DisplayMode mode)
{
    m_displayMode = mode;
    update();
}

// Drag & Drop
void ColorPreview::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasColor()) {
        event->acceptProposedAction();
    }
}

void ColorPreview::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasColor()) {
        setColor(qvariant_cast<QColor>(event->mimeData()->colorData()));
        event->acceptProposedAction();
    }
}

}  // namespace colorwidgets
}  // namespace snaptray
