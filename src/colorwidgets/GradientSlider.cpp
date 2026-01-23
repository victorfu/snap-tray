#include "colorwidgets/GradientSlider.h"

#include "colorwidgets/ColorUtils.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionSlider>

namespace snaptray {
namespace colorwidgets {

GradientSlider::GradientSlider(QWidget* parent) : GradientSlider(Qt::Horizontal, parent) {}

GradientSlider::GradientSlider(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    // Default black to white gradient
    m_gradient << qMakePair(0.0, QColor(Qt::black)) << qMakePair(1.0, QColor(Qt::white));

    setMinimumSize(orientation == Qt::Horizontal ? QSize(100, 24) : QSize(24, 100));
}

void GradientSlider::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRect groove = grooveRect();

    // 1. Draw checkerboard background (for showing transparency)
    drawCheckerboard(painter, groove);

    // 2. Draw gradient
    QLinearGradient grad;
    if (orientation() == Qt::Horizontal) {
        grad = QLinearGradient(groove.left(), 0, groove.right(), 0);
    } else {
        grad = QLinearGradient(0, groove.bottom(), 0, groove.top());
    }
    grad.setStops(m_gradient);

    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawRoundedRect(groove, 4, 4);

    // 3. Draw border
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(groove, 4, 4);

    // 4. Draw handle
    QRect handle = handleRect();

    // Handle color auto-selects black or white based on background
    QColor hColor = handleColor();

    painter.setPen(QPen(hColor == Qt::white ? Qt::black : Qt::white, 1));
    painter.setBrush(hColor);
    painter.drawRoundedRect(handle, 3, 3);
}

void GradientSlider::drawCheckerboard(QPainter& painter, const QRect& rect)
{
    static const int CHECKER_SIZE = 6;
    static const QColor CHECKER_LIGHT(200, 200, 200);
    static const QColor CHECKER_DARK(150, 150, 150);

    painter.save();

    // Use rounded rect clip path to match the groove shape
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, 4, 4);
    painter.setClipPath(clipPath);

    for (int y = rect.top(); y < rect.bottom(); y += CHECKER_SIZE) {
        for (int x = rect.left(); x < rect.right(); x += CHECKER_SIZE) {
            bool light = ((x / CHECKER_SIZE) + (y / CHECKER_SIZE)) % 2 == 0;
            painter.fillRect(x, y, CHECKER_SIZE, CHECKER_SIZE, light ? CHECKER_LIGHT : CHECKER_DARK);
        }
    }

    painter.restore();
}

QColor GradientSlider::handleColor() const
{
    // Calculate background color luminance at current position
    qreal pos = (value() - minimum()) / qreal(maximum() - minimum());

    // Find color at corresponding position in gradient
    QColor bg;
    for (int i = 0; i < m_gradient.size() - 1; ++i) {
        if (pos >= m_gradient[i].first && pos <= m_gradient[i + 1].first) {
            qreal t = (pos - m_gradient[i].first) / (m_gradient[i + 1].first - m_gradient[i].first);
            QColor c1 = m_gradient[i].second;
            QColor c2 = m_gradient[i + 1].second;
            bg = QColor(c1.red() + t * (c2.red() - c1.red()),
                        c1.green() + t * (c2.green() - c1.green()),
                        c1.blue() + t * (c2.blue() - c1.blue()));
            break;
        }
    }

    // Choose black or white based on luminance
    return ColorUtils::colorLumaF(bg) > 0.5 ? Qt::black : Qt::white;
}

QRect GradientSlider::grooveRect() const
{
    if (orientation() == Qt::Horizontal) {
        int y = (height() - GROOVE_HEIGHT) / 2;
        return QRect(HANDLE_WIDTH / 2, y, width() - HANDLE_WIDTH, GROOVE_HEIGHT);
    } else {
        int x = (width() - GROOVE_HEIGHT) / 2;
        return QRect(x, HANDLE_WIDTH / 2, GROOVE_HEIGHT, height() - HANDLE_WIDTH);
    }
}

QRect GradientSlider::handleRect() const
{
    QRect groove = grooveRect();
    qreal pos = (value() - minimum()) / qreal(maximum() - minimum());

    if (orientation() == Qt::Horizontal) {
        int x = groove.left() + pos * groove.width() - HANDLE_WIDTH / 2;
        return QRect(x, groove.top() - 2, HANDLE_WIDTH, groove.height() + 4);
    } else {
        int y = groove.bottom() - pos * groove.height() - HANDLE_WIDTH / 2;
        return QRect(groove.left() - 2, y, groove.width() + 4, HANDLE_WIDTH);
    }
}

void GradientSlider::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setValue(valueFromPos(event->pos()));
    }
}

void GradientSlider::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        setValue(valueFromPos(event->pos()));
    }
}

int GradientSlider::valueFromPos(const QPoint& pos) const
{
    QRect groove = grooveRect();
    qreal ratio;

    if (orientation() == Qt::Horizontal) {
        ratio = qreal(pos.x() - groove.left()) / groove.width();
    } else {
        ratio = 1.0 - qreal(pos.y() - groove.top()) / groove.height();
    }

    ratio = qBound(0.0, ratio, 1.0);
    return minimum() + qRound(ratio * (maximum() - minimum()));
}

// Getter/Setter implementation
QGradientStops GradientSlider::gradient() const
{
    return m_gradient;
}

void GradientSlider::setGradient(const QGradientStops& stops)
{
    m_gradient = stops;
    update();
}

QColor GradientSlider::firstColor() const
{
    return m_gradient.isEmpty() ? QColor() : m_gradient.first().second;
}

void GradientSlider::setFirstColor(const QColor& color)
{
    if (m_gradient.isEmpty()) {
        m_gradient << qMakePair(0.0, color) << qMakePair(1.0, QColor(Qt::white));
    } else {
        m_gradient[0].second = color;
    }
    update();
}

QColor GradientSlider::lastColor() const
{
    return m_gradient.isEmpty() ? QColor() : m_gradient.last().second;
}

void GradientSlider::setLastColor(const QColor& color)
{
    if (m_gradient.isEmpty()) {
        m_gradient << qMakePair(0.0, QColor(Qt::black)) << qMakePair(1.0, color);
    } else {
        m_gradient.last().second = color;
    }
    update();
}

}  // namespace colorwidgets
}  // namespace snaptray
