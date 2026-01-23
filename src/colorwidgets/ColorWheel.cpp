#include "colorwidgets/ColorWheel.h"

#include "colorwidgets/ColorUtils.h"

#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace snaptray {
namespace colorwidgets {

ColorWheel::ColorWheel(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(100, 100);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

ColorWheel::~ColorWheel() = default;

QSize ColorWheel::sizeHint() const
{
    return QSize(200, 200);
}

QSize ColorWheel::minimumSizeHint() const
{
    return QSize(100, 100);
}

QColor ColorWheel::color() const
{
    switch (m_colorSpace) {
        case ColorHSV:
            return QColor::fromHsv(m_hue, m_saturation, m_value);
        case ColorHSL:
            return QColor::fromHsl(m_hue, m_saturation, m_value);
        case ColorLCH:
            return ColorUtils::fromLch(m_value * 100.0 / 255.0, m_saturation * 150.0 / 255.0,
                                       m_hue);
    }
    return QColor();
}

void ColorWheel::setColor(const QColor& c)
{
    int h, s, v;

    switch (m_colorSpace) {
        case ColorHSV:
            h = c.hsvHue();
            s = c.hsvSaturation();
            v = c.value();
            break;
        case ColorHSL:
            h = c.hslHue();
            s = c.hslSaturation();
            v = c.lightness();
            break;
        case ColorLCH: {
            qreal l, ch, hue;
            ColorUtils::rgbToLch(c, l, ch, hue);
            h = qRound(hue);
            s = qRound(ch * 255.0 / 150.0);
            v = qRound(l * 255.0 / 100.0);
            break;
        }
    }

    if (h < 0)
        h = 0;

    bool changed = false;
    if (m_hue != h) {
        m_hue = h;
        changed = true;
        emit hueChanged(h);
    }
    if (m_saturation != s) {
        m_saturation = s;
        changed = true;
        emit saturationChanged(s);
    }
    if (m_value != v) {
        m_value = v;
        changed = true;
        emit valueChanged(v);
    }

    if (changed) {
        m_selectorDirty = true;
        emit colorChanged(color());
        update();
    }
}

// ========== Rendering ==========

void ColorWheel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPointF center = wheelCenter();

    // 1. Render wheel ring (if needs rebuild)
    if (m_wheelDirty || m_wheelImage.isNull()) {
        renderWheel();
    }

    // Draw wheel
    painter.drawImage(QPointF(0, 0), m_wheelImage);

    // 2. Render selector
    if (m_selectorDirty || m_selectorImage.isNull()) {
        if (m_shape == ShapeSquare) {
            renderSquare();
        } else {
            renderTriangle();
        }
    }

    // Draw selector
    if (m_shape == ShapeTriangle && m_rotating) {
        // Rotate triangle
        painter.save();
        painter.translate(center);
        painter.rotate(m_hue);
        painter.translate(-center);
        painter.drawImage(QPointF(0, 0), m_selectorImage);
        painter.restore();
    } else {
        painter.drawImage(QPointF(0, 0), m_selectorImage);
    }

    // 3. Draw hue indicator (small circle on wheel)
    QPointF huePos = hueIndicatorPos();
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(huePos, 6, 6);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawEllipse(huePos, 7, 7);

    // 4. Draw selector indicator (small circle on inner selector)
    QPointF selPos = selectorIndicatorPos();
    QColor indicatorColor = (ColorUtils::colorLumaF(color()) > 0.5) ? Qt::black : Qt::white;
    painter.setPen(QPen(indicatorColor, 2));
    painter.drawEllipse(selPos, 5, 5);
}

void ColorWheel::renderWheel()
{
    qreal dpr = devicePixelRatioF();
    int size = qMin(width(), height());

    m_wheelImage = QImage(QSize(size, size) * dpr, QImage::Format_ARGB32_Premultiplied);
    m_wheelImage.setDevicePixelRatio(dpr);
    m_wheelImage.fill(Qt::transparent);

    QPainter p(&m_wheelImage);
    p.setRenderHint(QPainter::Antialiasing);

    QPointF center = wheelCenter();
    qreal outer = outerRadius();
    qreal inner = innerRadius();

    // Draw wheel ring (using conical gradient)
    QConicalGradient gradient(center, 0);
    for (int i = 0; i <= 360; i += 1) {
        QColor c;
        switch (m_colorSpace) {
            case ColorHSV:
                c = QColor::fromHsv(i % 360, 255, 255);
                break;
            case ColorHSL:
                c = QColor::fromHsl(i % 360, 255, 128);
                break;
            case ColorLCH:
                c = ColorUtils::fromLch(65, 100, i % 360);  // Fixed L=65, C=100
                break;
        }
        gradient.setColorAt(i / 360.0, c);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(gradient);

    // Draw ring
    QPainterPath path;
    path.addEllipse(center, outer, outer);
    path.addEllipse(center, inner, inner);
    p.drawPath(path);

    m_wheelDirty = false;
}

void ColorWheel::renderSquare()
{
    qreal dpr = devicePixelRatioF();
    int size = qMin(width(), height());

    m_selectorImage = QImage(QSize(size, size) * dpr, QImage::Format_ARGB32_Premultiplied);
    m_selectorImage.setDevicePixelRatio(dpr);
    m_selectorImage.fill(Qt::transparent);

    QPointF center = wheelCenter();
    qreal selRadius = selectorRadius();

    // Square's top-left and size
    qreal side = selRadius * std::sqrt(2);
    QRectF squareRect(center.x() - side / 2, center.y() - side / 2, side, side);

    // Create S-V gradient image
    int sqSize = qRound(side * dpr);
    QImage sqImage(sqSize, sqSize, QImage::Format_ARGB32);

    for (int y = 0; y < sqSize; ++y) {
        int v = 255 - y * 255 / (sqSize - 1);  // Top to bottom: 255 → 0
        QRgb* line = reinterpret_cast<QRgb*>(sqImage.scanLine(y));
        for (int x = 0; x < sqSize; ++x) {
            int s = x * 255 / (sqSize - 1);  // Left to right: 0 → 255
            QColor c;
            switch (m_colorSpace) {
                case ColorHSV:
                    c = QColor::fromHsv(m_hue, s, v);
                    break;
                case ColorHSL:
                    c = QColor::fromHsl(m_hue, s, v);
                    break;
                case ColorLCH:
                    c = ColorUtils::fromLch(v * 100.0 / 255.0, s * 150.0 / 255.0, m_hue);
                    break;
            }
            line[x] = c.rgba();
        }
    }

    QPainter p(&m_selectorImage);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(squareRect, sqImage);

    m_selectorDirty = false;
}

void ColorWheel::renderTriangle()
{
    qreal dpr = devicePixelRatioF();
    int size = qMin(width(), height());

    m_selectorImage = QImage(QSize(size, size) * dpr, QImage::Format_ARGB32_Premultiplied);
    m_selectorImage.setDevicePixelRatio(dpr);
    m_selectorImage.fill(Qt::transparent);

    QPointF center = wheelCenter();
    qreal radius = selectorRadius();

    // Triangle vertices (pointing right, will be rotated based on hue)
    // Vertex A: Pure color (S=255, V=255)
    // Vertex B: White (S=0, V=255)
    // Vertex C: Black (S=0, V=0)
    qreal angle = m_rotating ? 0 : (m_hue * M_PI / 180.0);

    QPointF pA(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
    QPointF pB(center.x() + radius * std::cos(angle + 2 * M_PI / 3),
               center.y() + radius * std::sin(angle + 2 * M_PI / 3));
    QPointF pC(center.x() + radius * std::cos(angle + 4 * M_PI / 3),
               center.y() + radius * std::sin(angle + 4 * M_PI / 3));

    // Calculate triangle's bounding box
    qreal minX = std::min({pA.x(), pB.x(), pC.x()});
    qreal maxX = std::max({pA.x(), pB.x(), pC.x()});
    qreal minY = std::min({pA.y(), pB.y(), pC.y()});
    qreal maxY = std::max({pA.y(), pB.y(), pC.y()});

    // Calculate color for each pixel
    QPainter p(&m_selectorImage);
    p.setRenderHint(QPainter::Antialiasing);

    for (int y = qFloor(minY * dpr); y <= qCeil(maxY * dpr); ++y) {
        for (int x = qFloor(minX * dpr); x <= qCeil(maxX * dpr); ++x) {
            QPointF pt(x / dpr, y / dpr);

            // Calculate barycentric coordinates
            qreal denom =
                (pB.y() - pC.y()) * (pA.x() - pC.x()) + (pC.x() - pB.x()) * (pA.y() - pC.y());
            qreal wA =
                ((pB.y() - pC.y()) * (pt.x() - pC.x()) + (pC.x() - pB.x()) * (pt.y() - pC.y())) /
                denom;
            qreal wB =
                ((pC.y() - pA.y()) * (pt.x() - pC.x()) + (pA.x() - pC.x()) * (pt.y() - pC.y())) /
                denom;
            qreal wC = 1 - wA - wB;

            // Check if inside triangle
            if (wA >= 0 && wB >= 0 && wC >= 0) {
                // A: S=255, V=255; B: S=0, V=255; C: S=0, V=0
                int s = qRound(wA * 255);
                int v = qRound((wA + wB) * 255);

                QColor c;
                switch (m_colorSpace) {
                    case ColorHSV:
                        c = QColor::fromHsv(m_hue, s, v);
                        break;
                    case ColorHSL:
                        c = QColor::fromHsl(m_hue, s, v);
                        break;
                    case ColorLCH:
                        c = ColorUtils::fromLch(v * 100.0 / 255.0, s * 150.0 / 255.0, m_hue);
                        break;
                }

                p.setPen(c);
                p.drawPoint(pt);
            }
        }
    }

    m_selectorDirty = false;
}

// ========== Mouse Interaction ==========

void ColorWheel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    QPointF pos = event->position();

    if (isInHueRing(pos)) {
        m_dragMode = DragHue;
        setHueFromPos(pos);
    } else if (isInSelector(pos)) {
        m_dragMode = DragSquare;
        setColorFromPos(pos);
    }
}

void ColorWheel::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->position();

    switch (m_dragMode) {
        case DragHue:
            setHueFromPos(pos);
            break;
        case DragSquare:
            setColorFromPos(pos);
            break;
        default:
            break;
    }
}

void ColorWheel::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragMode != DragNone) {
        m_dragMode = DragNone;
        emit colorSelected(color());
    }
}

bool ColorWheel::isInHueRing(const QPointF& pos) const
{
    QPointF center = wheelCenter();
    qreal dist = std::hypot(pos.x() - center.x(), pos.y() - center.y());
    return dist >= innerRadius() && dist <= outerRadius();
}

bool ColorWheel::isInSelector(const QPointF& pos) const
{
    QPointF center = wheelCenter();
    qreal dist = std::hypot(pos.x() - center.x(), pos.y() - center.y());
    return dist < innerRadius() - SELECTOR_MARGIN;
}

void ColorWheel::setHueFromPos(const QPointF& pos)
{
    QPointF center = wheelCenter();
    qreal angle = std::atan2(pos.y() - center.y(), pos.x() - center.x());
    int h = qRound(angle * 180.0 / M_PI);
    if (h < 0)
        h += 360;

    setHue(h);
}

void ColorWheel::setColorFromPos(const QPointF& pos)
{
    QPointF center = wheelCenter();
    qreal radius = selectorRadius();

    if (m_shape == ShapeSquare) {
        qreal side = radius * std::sqrt(2);
        qreal left = center.x() - side / 2;
        qreal top = center.y() - side / 2;

        qreal x = qBound(0.0, (pos.x() - left) / side, 1.0);
        qreal y = qBound(0.0, (pos.y() - top) / side, 1.0);

        int s = qRound(x * 255);
        int v = qRound((1 - y) * 255);

        setSaturation(s);
        setValue(v);
    } else {
        // Triangle selector - calculate barycentric coordinates
        qreal angle = m_rotating ? 0 : (m_hue * M_PI / 180.0);

        QPointF pA(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
        QPointF pB(center.x() + radius * std::cos(angle + 2 * M_PI / 3),
                   center.y() + radius * std::sin(angle + 2 * M_PI / 3));
        QPointF pC(center.x() + radius * std::cos(angle + 4 * M_PI / 3),
                   center.y() + radius * std::sin(angle + 4 * M_PI / 3));

        // For rotating triangle, we need to account for rotation
        QPointF adjustedPos = pos;
        if (m_rotating) {
            // Rotate the position backwards by hue degrees
            qreal hueRad = -m_hue * M_PI / 180.0;
            qreal dx = pos.x() - center.x();
            qreal dy = pos.y() - center.y();
            adjustedPos = QPointF(center.x() + dx * std::cos(hueRad) - dy * std::sin(hueRad),
                                  center.y() + dx * std::sin(hueRad) + dy * std::cos(hueRad));
        }

        // Calculate barycentric coordinates
        qreal denom =
            (pB.y() - pC.y()) * (pA.x() - pC.x()) + (pC.x() - pB.x()) * (pA.y() - pC.y());

        if (qAbs(denom) < 0.001)
            return;  // Avoid division by zero

        qreal wA = ((pB.y() - pC.y()) * (adjustedPos.x() - pC.x()) +
                    (pC.x() - pB.x()) * (adjustedPos.y() - pC.y())) /
                   denom;
        qreal wB = ((pC.y() - pA.y()) * (adjustedPos.x() - pC.x()) +
                    (pA.x() - pC.x()) * (adjustedPos.y() - pC.y())) /
                   denom;
        qreal wC = 1 - wA - wB;

        // Constrain to triangle
        wA = qBound(0.0, wA, 1.0);
        wB = qBound(0.0, wB, 1.0);
        wC = qBound(0.0, wC, 1.0);

        // Renormalize
        qreal sum = wA + wB + wC;
        if (sum > 0) {
            wA /= sum;
            wB /= sum;
            wC /= sum;
        }

        // Calculate S, V from barycentric coordinates
        // V = wA + wB (non-black weight)
        // S = wA / V (pure color ratio in non-black)
        qreal v = wA + wB;
        qreal s = (v > 0.001) ? wA / v : 0;

        setSaturation(qRound(s * 255));
        setValue(qRound(v * 255));
    }
}

// ========== Geometry Calculations ==========

QPointF ColorWheel::wheelCenter() const
{
    return QPointF(width() / 2.0, height() / 2.0);
}

qreal ColorWheel::outerRadius() const
{
    return qMin(width(), height()) / 2.0 - 2;
}

qreal ColorWheel::innerRadius() const
{
    return outerRadius() - WHEEL_THICKNESS;
}

qreal ColorWheel::selectorRadius() const
{
    return innerRadius() - SELECTOR_MARGIN;
}

QPointF ColorWheel::hueIndicatorPos() const
{
    QPointF center = wheelCenter();
    qreal r = (outerRadius() + innerRadius()) / 2;
    qreal angle = m_hue * M_PI / 180.0;
    return QPointF(center.x() + r * std::cos(angle), center.y() + r * std::sin(angle));
}

QPointF ColorWheel::selectorIndicatorPos() const
{
    QPointF center = wheelCenter();
    qreal radius = selectorRadius();

    if (m_shape == ShapeSquare) {
        qreal side = radius * std::sqrt(2);
        qreal left = center.x() - side / 2;
        qreal top = center.y() - side / 2;

        qreal x = left + m_saturation / 255.0 * side;
        qreal y = top + (1 - m_value / 255.0) * side;

        return QPointF(x, y);
    } else {
        // Triangle selector
        // Triangle vertices: A=pure color, B=white, C=black
        qreal angle = m_rotating ? 0 : (m_hue * M_PI / 180.0);

        QPointF pA(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
        QPointF pB(center.x() + radius * std::cos(angle + 2 * M_PI / 3),
                   center.y() + radius * std::sin(angle + 2 * M_PI / 3));
        QPointF pC(center.x() + radius * std::cos(angle + 4 * M_PI / 3),
                   center.y() + radius * std::sin(angle + 4 * M_PI / 3));

        // Calculate barycentric coordinates from S, V
        qreal s = m_saturation / 255.0;
        qreal v = m_value / 255.0;

        qreal wA = s * v;
        qreal wB = (1 - s) * v;
        qreal wC = 1 - v;

        QPointF result(wA * pA.x() + wB * pB.x() + wC * pC.x(),
                       wA * pA.y() + wB * pB.y() + wC * pC.y());

        // For rotating triangle, rotate the result
        if (m_rotating) {
            qreal hueRad = m_hue * M_PI / 180.0;
            qreal dx = result.x() - center.x();
            qreal dy = result.y() - center.y();
            result = QPointF(center.x() + dx * std::cos(hueRad) - dy * std::sin(hueRad),
                             center.y() + dx * std::sin(hueRad) + dy * std::cos(hueRad));
        }

        return result;
    }
}

QPolygonF ColorWheel::triangleVertices() const
{
    QPointF center = wheelCenter();
    qreal radius = selectorRadius();
    qreal angle = m_rotating ? 0 : (m_hue * M_PI / 180.0);

    QPolygonF vertices;
    vertices << QPointF(center.x() + radius * std::cos(angle),
                        center.y() + radius * std::sin(angle));
    vertices << QPointF(center.x() + radius * std::cos(angle + 2 * M_PI / 3),
                        center.y() + radius * std::sin(angle + 2 * M_PI / 3));
    vertices << QPointF(center.x() + radius * std::cos(angle + 4 * M_PI / 3),
                        center.y() + radius * std::sin(angle + 4 * M_PI / 3));
    return vertices;
}

// ========== Getter/Setter Methods ==========

int ColorWheel::hue() const
{
    return m_hue;
}

void ColorWheel::setHue(int hue)
{
    hue = qBound(0, hue, 359);
    if (m_hue != hue) {
        m_hue = hue;
        m_selectorDirty = true;
        emit hueChanged(hue);
        emit colorChanged(color());
        update();
    }
}

int ColorWheel::saturation() const
{
    return m_saturation;
}

void ColorWheel::setSaturation(int saturation)
{
    saturation = qBound(0, saturation, 255);
    if (m_saturation != saturation) {
        m_saturation = saturation;
        emit saturationChanged(saturation);
        emit colorChanged(color());
        update();
    }
}

int ColorWheel::value() const
{
    return m_value;
}

void ColorWheel::setValue(int value)
{
    value = qBound(0, value, 255);
    if (m_value != value) {
        m_value = value;
        emit valueChanged(value);
        emit colorChanged(color());
        update();
    }
}

ColorWheel::ShapeEnum ColorWheel::wheelShape() const
{
    return m_shape;
}

void ColorWheel::setWheelShape(ShapeEnum shape)
{
    if (m_shape != shape) {
        m_shape = shape;
        m_selectorDirty = true;
        update();
    }
}

ColorWheel::ColorSpaceEnum ColorWheel::colorSpace() const
{
    return m_colorSpace;
}

void ColorWheel::setColorSpace(ColorSpaceEnum space)
{
    if (m_colorSpace != space) {
        m_colorSpace = space;
        m_wheelDirty = true;
        m_selectorDirty = true;
        update();
    }
}

bool ColorWheel::wheelRotating() const
{
    return m_rotating;
}

void ColorWheel::setWheelRotating(bool rotating)
{
    if (m_rotating != rotating) {
        m_rotating = rotating;
        m_selectorDirty = true;
        update();
    }
}

void ColorWheel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_wheelDirty = true;
    m_selectorDirty = true;
}

// ========== Drag & Drop Support ==========

void ColorWheel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasColor()) {
        event->acceptProposedAction();
    }
}

void ColorWheel::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasColor()) {
        QColor c = qvariant_cast<QColor>(event->mimeData()->colorData());
        setColor(c);
        emit colorSelected(c);
        event->acceptProposedAction();
    }
}

}  // namespace colorwidgets
}  // namespace snaptray
