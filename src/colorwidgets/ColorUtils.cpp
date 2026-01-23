#include "colorwidgets/ColorUtils.h"

#include <QApplication>
#include <QCursor>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QScreen>

#include <cmath>

namespace snaptray {
namespace colorwidgets {

// D65 white point reference values
static constexpr qreal D65_X = 95.047;
static constexpr qreal D65_Y = 100.0;
static constexpr qreal D65_Z = 108.883;

QColor ColorUtils::fromHsv(int h, int s, int v, int a)
{
    return QColor::fromHsv(qBound(0, h, 359), qBound(0, s, 255), qBound(0, v, 255),
                           qBound(0, a, 255));
}

QColor ColorUtils::fromHsl(int h, int s, int l, int a)
{
    return QColor::fromHsl(qBound(0, h, 359), qBound(0, s, 255), qBound(0, l, 255),
                           qBound(0, a, 255));
}

void ColorUtils::rgbToXyz(const QColor& color, qreal& x, qreal& y, qreal& z)
{
    // sRGB → linear RGB
    auto toLinear = [](qreal c) -> qreal {
        c /= 255.0;
        return (c > 0.04045) ? std::pow((c + 0.055) / 1.055, 2.4) : c / 12.92;
    };

    qreal r = toLinear(color.red());
    qreal g = toLinear(color.green());
    qreal b = toLinear(color.blue());

    // Linear RGB → XYZ (sRGB, D65)
    x = r * 41.24564 + g * 35.75761 + b * 18.04375;
    y = r * 21.26729 + g * 71.51522 + b * 7.21750;
    z = r * 1.93339 + g * 11.91920 + b * 95.03041;
}

void ColorUtils::xyzToLab(qreal x, qreal y, qreal z, qreal& l, qreal& a, qreal& b)
{
    auto f = [](qreal t) -> qreal {
        constexpr qreal delta = 6.0 / 29.0;
        return (t > delta * delta * delta) ? std::cbrt(t)
                                           : t / (3 * delta * delta) + 4.0 / 29.0;
    };

    qreal fx = f(x / D65_X);
    qreal fy = f(y / D65_Y);
    qreal fz = f(z / D65_Z);

    l = 116.0 * fy - 16.0;
    a = 500.0 * (fx - fy);
    b = 200.0 * (fy - fz);
}

void ColorUtils::rgbToLch(const QColor& color, qreal& l, qreal& c, qreal& h)
{
    qreal x, y, z;
    rgbToXyz(color, x, y, z);

    qreal la, a, b;
    xyzToLab(x, y, z, la, a, b);

    l = la;
    c = std::sqrt(a * a + b * b);
    h = std::atan2(b, a) * 180.0 / M_PI;
    if (h < 0)
        h += 360.0;
}

void ColorUtils::labToXyz(qreal l, qreal a, qreal b, qreal& x, qreal& y, qreal& z)
{
    auto fInv = [](qreal t) -> qreal {
        constexpr qreal delta = 6.0 / 29.0;
        return (t > delta) ? t * t * t : 3 * delta * delta * (t - 4.0 / 29.0);
    };

    qreal fy = (l + 16.0) / 116.0;
    qreal fx = a / 500.0 + fy;
    qreal fz = fy - b / 200.0;

    x = D65_X * fInv(fx);
    y = D65_Y * fInv(fy);
    z = D65_Z * fInv(fz);
}

QColor ColorUtils::xyzToRgb(qreal x, qreal y, qreal z, int alpha)
{
    // XYZ → linear RGB
    qreal r = x * 3.2404542 + y * -1.5371385 + z * -0.4985314;
    qreal g = x * -0.9692660 + y * 1.8760108 + z * 0.0415560;
    qreal b = x * 0.0556434 + y * -0.2040259 + z * 1.0572252;

    // Linear RGB → sRGB
    auto toSrgb = [](qreal c) -> int {
        c /= 100.0;
        c = (c > 0.0031308) ? 1.055 * std::pow(c, 1.0 / 2.4) - 0.055 : 12.92 * c;
        return qBound(0, qRound(c * 255.0), 255);
    };

    return QColor(toSrgb(r), toSrgb(g), toSrgb(b), alpha);
}

QColor ColorUtils::fromLch(qreal l, qreal c, qreal h, int a)
{
    qreal hRad = h * M_PI / 180.0;
    qreal la = c * std::cos(hRad);
    qreal lb = c * std::sin(hRad);

    qreal x, y, z;
    labToXyz(l, la, lb, x, y, z);

    return xyzToRgb(x, y, z, a);
}

QColor ColorUtils::fromString(const QString& str)
{
    QString s = str.trimmed().toLower();

    // Remove # prefix
    if (s.startsWith('#')) {
        s = s.mid(1);
    }

    // 3-digit hex: #f00 → #ff0000
    static QRegularExpression hex3Re("^[0-9a-f]{3}$");
    if (s.length() == 3 && hex3Re.match(s).hasMatch()) {
        s = QString("%1%1%2%2%3%3").arg(s[0]).arg(s[1]).arg(s[2]);
    }

    // 6-digit hex: ff0000
    static QRegularExpression hex6Re("^[0-9a-f]{6}$");
    if (s.length() == 6 && hex6Re.match(s).hasMatch()) {
        return QColor('#' + s);
    }

    // 8-digit hex with alpha: ff0000ff
    static QRegularExpression hex8Re("^[0-9a-f]{8}$");
    if (s.length() == 8 && hex8Re.match(s).hasMatch()) {
        int r = s.mid(0, 2).toInt(nullptr, 16);
        int g = s.mid(2, 2).toInt(nullptr, 16);
        int b = s.mid(4, 2).toInt(nullptr, 16);
        int a = s.mid(6, 2).toInt(nullptr, 16);
        return QColor(r, g, b, a);
    }

    // rgb(r, g, b)
    static QRegularExpression rgbRe(R"(rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))");
    auto match = rgbRe.match(s);
    if (match.hasMatch()) {
        return QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                      match.captured(3).toInt());
    }

    // rgba(r, g, b, a)
    static QRegularExpression rgbaRe(
        R"(rgba\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))");
    match = rgbaRe.match(s);
    if (match.hasMatch()) {
        return QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                      match.captured(3).toInt(), match.captured(4).toInt());
    }

    // CSS color names
    QColor namedColor(str);
    if (namedColor.isValid()) {
        return namedColor;
    }

    return QColor();  // Invalid color
}

QString ColorUtils::toString(const QColor& color, bool includeAlpha)
{
    if (includeAlpha && color.alpha() < 255) {
        return QString("#%1%2%3%4")
            .arg(color.red(), 2, 16, QChar('0'))
            .arg(color.green(), 2, 16, QChar('0'))
            .arg(color.blue(), 2, 16, QChar('0'))
            .arg(color.alpha(), 2, 16, QChar('0'))
            .toUpper();
    }
    return color.name().toUpper();
}

qreal ColorUtils::colorLumaF(const QColor& color)
{
    // ITU-R BT.709
    return (0.2126 * color.redF() + 0.7152 * color.greenF() + 0.0722 * color.blueF());
}

qreal ColorUtils::colorChromaF(const QColor& color)
{
    qreal r = color.redF();
    qreal g = color.greenF();
    qreal b = color.blueF();
    qreal max = qMax(r, qMax(g, b));
    qreal min = qMin(r, qMin(g, b));
    return max - min;
}

qreal ColorUtils::colorLightnessF(const QColor& color)
{
    return color.lightnessF();
}

// ========== Screen Color Picker Implementation ==========

class ScreenColorPicker : public QWidget
{
public:
    ScreenColorPicker() : m_color(Qt::white)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);

        // Capture entire screen
        QScreen* screen = QGuiApplication::primaryScreen();
        m_screenshot = screen->grabWindow(0);
        setGeometry(screen->geometry());

        show();
        grabMouse();
        grabKeyboard();
    }

    QColor color() const { return m_color; }
    bool accepted() const { return m_accepted; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.drawPixmap(0, 0, m_screenshot);

        // Draw magnifier
        QPoint pos = mapFromGlobal(QCursor::pos());
        int magSize = 120;
        int magZoom = 8;
        int srcSize = magSize / magZoom;

        QRect srcRect(pos.x() - srcSize / 2, pos.y() - srcSize / 2, srcSize, srcSize);
        QRect dstRect(pos.x() + 20, pos.y() + 20, magSize, magSize);

        // Ensure magnifier doesn't exceed screen bounds
        if (dstRect.right() > width())
            dstRect.moveLeft(pos.x() - magSize - 20);
        if (dstRect.bottom() > height())
            dstRect.moveTop(pos.y() - magSize - 20);

        // Draw magnifier background
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(Qt::black);
        p.drawRect(dstRect.adjusted(-2, -2, 2, 2));

        // Draw magnified image
        p.drawPixmap(dstRect, m_screenshot, srcRect);

        // Draw crosshair
        int cx = dstRect.center().x();
        int cy = dstRect.center().y();
        p.setPen(QPen(Qt::white, 1));
        p.drawLine(cx - 10, cy, cx + 10, cy);
        p.drawLine(cx, cy - 10, cx, cy + 10);

        // Show color value
        p.setPen(Qt::white);
        p.drawText(dstRect.adjusted(4, 4, -4, -4), Qt::AlignBottom | Qt::AlignHCenter,
                   m_color.name().toUpper());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        QPoint pos = event->pos();
        QImage img = m_screenshot.toImage();
        if (img.valid(pos)) {
            m_color = img.pixelColor(pos);
        }
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_accepted = true;
            close();
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Escape) {
            m_accepted = false;
            close();
        }
    }

private:
    QPixmap m_screenshot;
    QColor m_color;
    bool m_accepted = false;
};

QColor ColorUtils::getScreenColor(QWidget*)
{
    ScreenColorPicker picker;

    // Enter event loop until closed
    while (picker.isVisible()) {
        QApplication::processEvents();
    }

    return picker.accepted() ? picker.color() : QColor();
}

}  // namespace colorwidgets
}  // namespace snaptray
