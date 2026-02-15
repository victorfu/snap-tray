#include "colorwidgets/ColorWheel.h"

#include "colorwidgets/ColorUtils.h"
#include "utils/CoordinateHelper.h"

#include <QDrag>
#include <QLineF>
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
    painter.translate(width() / 2.0, height() / 2.0);

    // 1. Render and draw wheel ring
    if (m_wheelDirty || m_wheelImage.isNull()) {
        renderWheel();
    }
    qreal outer = outerRadius();
    painter.drawPixmap(QRectF(-outer, -outer, outer * 2.0, outer * 2.0), m_wheelImage,
                       m_wheelImage.rect());

    // 2. Draw hue indicator (line from inner to outer radius)
    painter.setPen(QPen(Qt::black, 3));
    painter.setBrush(Qt::NoBrush);
    QLineF ray(0, 0, outer, 0);
    ray.setAngle(m_hue);  // QLineF uses counter-clockwise from east
    QPointF h1 = ray.p2();
    ray.setLength(innerRadius());
    QPointF h2 = ray.p2();
    painter.drawLine(h1, h2);

    // 3. Render and draw selector
    if (m_selectorDirty || m_selectorImage.isNull()) {
        if (m_shape == ShapeSquare) {
            renderSquare();
        } else {
            renderTriangle();
        }
    }

    // Apply rotation for selector
    if (m_rotating) {
        painter.rotate(selectorImageAngle());
    }
    painter.translate(selectorImageOffset());

    // Draw selector with clipping for triangle
    QPointF selectorPosition;
    if (m_shape == ShapeSquare) {
        qreal side = squareSize();
        selectorPosition = QPointF((m_saturation / 255.0) * side, (m_value / 255.0) * side);
    } else {
        qreal side = triangleSide();
        qreal height = triangleHeight();
        qreal val = m_value / 255.0;
        qreal sat = m_saturation / 255.0;
        qreal sliceH = side * val;
        qreal ymin = side / 2 - sliceH / 2;

        selectorPosition = QPointF(val * height, ymin + sat * sliceH);

        // Clip to triangle
        QPolygonF triangle;
        triangle.append(QPointF(0, side / 2));
        triangle.append(QPointF(height, 0));
        triangle.append(QPointF(height, side));
        QPainterPath clip;
        clip.addPolygon(triangle);
        painter.setClipPath(clip);
    }

    painter.drawImage(QRectF(QPointF(0, 0), selectorSize()), m_selectorImage);
    painter.setClipping(false);

    // 4. Draw selector indicator
    QColor indicatorColor = (m_value < 166 || m_saturation > 110) ? Qt::white : Qt::black;
    painter.setPen(QPen(indicatorColor, 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(selectorPosition, 6, 6);
}

void ColorWheel::renderWheel()
{
    const qreal dpr = devicePixelRatioF();
    const qreal outer = outerRadius();
    const int logicalSize = qRound(outer * 2);
    const QSize deviceSize = CoordinateHelper::toPhysical(QSize(logicalSize, logicalSize), dpr);

    m_wheelImage = QPixmap(deviceSize);
    m_wheelImage.setDevicePixelRatio(dpr);
    m_wheelImage.fill(Qt::transparent);

    QPainter painter(&m_wheelImage);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(outer, outer);

    // Hue gradient
    QConicalGradient gradient(0, 0, 0);
    const int hueStops = 24;
    for (int i = 0; i < hueStops; ++i) {
        double a = double(i) / (hueStops - 1);
        QColor c;
        switch (m_colorSpace) {
            case ColorHSV:
                c = QColor::fromHsv(qRound(a * 359), 255, 255);
                break;
            case ColorHSL:
                c = QColor::fromHsl(qRound(a * 359), 255, 128);
                break;
            case ColorLCH:
                c = ColorUtils::fromLch(65, 100, qRound(a * 359));
                break;
        }
        gradient.setColorAt(a, c);
    }
    gradient.setColorAt(1.0, QColor::fromHsv(0, 255, 255));

    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QPointF(0, 0), outer, outer);

    // Cut out inner circle
    painter.setBrush(Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.drawEllipse(QPointF(0, 0), innerRadius(), innerRadius());

    m_wheelDirty = false;
}

void ColorWheel::renderSquare()
{
    const qreal dpr = devicePixelRatioF();
    const int logicalSize = qRound(squareSize());
    int size = CoordinateHelper::toPhysical(QSize(logicalSize, logicalSize), dpr).width();
    size = qMin(size, 128);

    m_selectorImage = QImage(size, size, QImage::Format_RGB32);
    m_selectorImage.setDevicePixelRatio(dpr);

    for (int y = 0; y < size; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_selectorImage.scanLine(y));
        for (int x = 0; x < size; ++x) {
            qreal sat = double(x) / size;
            qreal val = double(y) / size;
            QColor c;
            switch (m_colorSpace) {
                case ColorHSV:
                    c = QColor::fromHsvF(m_hue / 359.0, sat, val);
                    break;
                case ColorHSL:
                    c = QColor::fromHslF(m_hue / 359.0, sat, val);
                    break;
                case ColorLCH:
                    c = ColorUtils::fromLch(val * 100, sat * 150, m_hue);
                    break;
            }
            line[x] = c.rgb();
        }
    }

    m_selectorDirty = false;
}

void ColorWheel::renderTriangle()
{
    qreal dpr = devicePixelRatioF();
    QSizeF size = selectorSize();
    if (size.height() > 128) {
        size *= 128.0 / size.height();
    }
    size *= dpr;

    qreal ycenter = size.height() / 2;
    QSize isize = size.toSize();

    m_selectorImage = QImage(isize, QImage::Format_RGB32);
    m_selectorImage.setDevicePixelRatio(dpr);

    for (int x = 0; x < isize.width(); ++x) {
        qreal pval = double(x) / size.height();
        qreal sliceH = size.height() * pval;
        for (int y = 0; y < isize.height(); ++y) {
            qreal ymin = ycenter - sliceH / 2;
            qreal psat = qBound(0.0, (y - ymin) / sliceH, 1.0);
            QColor c;
            switch (m_colorSpace) {
                case ColorHSV:
                    c = QColor::fromHsvF(m_hue / 359.0, psat, pval);
                    break;
                case ColorHSL:
                    c = QColor::fromHslF(m_hue / 359.0, psat, pval);
                    break;
                case ColorLCH:
                    c = ColorUtils::fromLch(pval * 100, psat * 150, m_hue);
                    break;
            }
            m_selectorImage.setPixel(x, y, c.rgb());
        }
    }

    m_selectorDirty = false;
}

// ========== Geometry ==========

qreal ColorWheel::outerRadius() const
{
    return qMin(width(), height()) / 2.0;
}

qreal ColorWheel::innerRadius() const
{
    return outerRadius() - WHEEL_THICKNESS;
}

qreal ColorWheel::squareSize() const
{
    return innerRadius() * std::sqrt(2.0);
}

qreal ColorWheel::triangleHeight() const
{
    return innerRadius() * 3.0 / 2.0;
}

qreal ColorWheel::triangleSide() const
{
    return innerRadius() * std::sqrt(3.0);
}

QSizeF ColorWheel::selectorSize() const
{
    if (m_shape == ShapeTriangle) {
        return QSizeF(triangleHeight(), triangleSide());
    }
    return QSizeF(squareSize(), squareSize());
}

QPointF ColorWheel::selectorImageOffset() const
{
    if (m_shape == ShapeTriangle) {
        return QPointF(-innerRadius(), -triangleSide() / 2);
    }
    return QPointF(-squareSize() / 2, -squareSize() / 2);
}

qreal ColorWheel::selectorImageAngle() const
{
    if (m_shape == ShapeTriangle) {
        if (m_rotating) {
            return -m_hue - 60;
        }
        return -150;
    } else {
        if (m_rotating) {
            return -m_hue - 45;
        }
        return 180;
    }
}

// ========== Mouse Interaction ==========

void ColorWheel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;

    QLineF ray = lineToPoint(event->pos());
    if (ray.length() <= innerRadius()) {
        m_dragMode = DragSquare;
    } else if (ray.length() <= outerRadius()) {
        m_dragMode = DragHue;
    }

    mouseMoveEvent(event);
}

void ColorWheel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragMode == DragHue) {
        QLineF ray = lineToPoint(event->pos());
        int h = qRound(ray.angle());
        if (h < 0)
            h += 360;
        if (h >= 360)
            h -= 360;
        setHue(h);
    } else if (m_dragMode == DragSquare) {
        QLineF globMouseLn = lineToPoint(event->pos());
        QLineF centerMouseLn(QPointF(0, 0),
                             QPointF(globMouseLn.x2() - globMouseLn.x1(),
                                     globMouseLn.y2() - globMouseLn.y1()));

        centerMouseLn.setAngle(centerMouseLn.angle() + selectorImageAngle());
        centerMouseLn.setP2(centerMouseLn.p2() - selectorImageOffset());

        if (m_shape == ShapeSquare) {
            qreal side = squareSize();
            int s = qRound(qBound(0.0, centerMouseLn.x2() / side, 1.0) * 255);
            int v = qRound(qBound(0.0, centerMouseLn.y2() / side, 1.0) * 255);
            setSaturation(s);
            setValue(v);
        } else {
            QPointF pt = centerMouseLn.p2();
            qreal side = triangleSide();
            qreal val = qBound(0.0, pt.x() / triangleHeight(), 1.0);
            qreal sliceH = side * val;
            qreal ycenter = side / 2;
            qreal ymin = ycenter - sliceH / 2;
            qreal sat = 0;
            if (sliceH > 0) {
                sat = qBound(0.0, (pt.y() - ymin) / sliceH, 1.0);
            }
            setSaturation(qRound(sat * 255));
            setValue(qRound(val * 255));
        }
    }
}

void ColorWheel::mouseReleaseEvent(QMouseEvent*)
{
    if (m_dragMode != DragNone) {
        m_dragMode = DragNone;
        emit colorSelected(color());
    }
}

QLineF ColorWheel::lineToPoint(const QPoint& p) const
{
    return QLineF(width() / 2.0, height() / 2.0, p.x(), p.y());
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
