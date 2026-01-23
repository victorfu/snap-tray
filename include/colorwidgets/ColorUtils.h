#ifndef SNAPTRAY_COLOR_UTILS_H
#define SNAPTRAY_COLOR_UTILS_H

#include <QColor>
#include <QString>

namespace snaptray {
namespace colorwidgets {

class ColorUtils
{
public:
    // ========== Color Space Conversion ==========

    // HSV → RGB (h: 0-359, s: 0-255, v: 0-255)
    static QColor fromHsv(int h, int s, int v, int a = 255);

    // HSL → RGB (h: 0-359, s: 0-255, l: 0-255)
    static QColor fromHsl(int h, int s, int l, int a = 255);

    // LCH → RGB (Perceptually uniform color space)
    // l: 0-100 (lightness), c: 0-150+ (chroma), h: 0-360 (hue)
    static QColor fromLch(qreal l, qreal c, qreal h, int a = 255);

    // RGB → LCH
    static void rgbToLch(const QColor& color, qreal& l, qreal& c, qreal& h);

    // ========== String Parsing ==========

    // Supported formats:
    // - "#f00", "#ff0000", "#ff0000ff"
    // - "f00", "ff0000", "ff0000ff"
    // - "red", "green", "blue" (CSS color names)
    // - "rgb(255, 0, 0)", "rgb(255,0,0)"
    // - "rgba(255, 0, 0, 128)"
    static QColor fromString(const QString& str);

    // Convert to string (with or without alpha)
    static QString toString(const QColor& color, bool includeAlpha = false);

    // ========== Color Calculations ==========

    // Perceptual luminance (0.0 - 1.0)
    // Uses ITU-R BT.709 formula: 0.2126*R + 0.7152*G + 0.0722*B
    static qreal colorLumaF(const QColor& color);

    // Chroma (0.0 - 1.0)
    static qreal colorChromaF(const QColor& color);

    // Lightness (0.0 - 1.0)
    static qreal colorLightnessF(const QColor& color);

private:
    // Lab intermediate conversion
    static void rgbToXyz(const QColor& color, qreal& x, qreal& y, qreal& z);
    static void xyzToLab(qreal x, qreal y, qreal z, qreal& l, qreal& a, qreal& b);
    static void labToXyz(qreal l, qreal a, qreal b, qreal& x, qreal& y, qreal& z);
    static QColor xyzToRgb(qreal x, qreal y, qreal z, int alpha);
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_COLOR_UTILS_H
