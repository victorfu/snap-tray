#pragma once

#include "beautify/BeautifySettings.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace SnapTray {

// A saved look shared by screenshot Beautify and the recording frame (#269):
// background, space around the picture, corners, shadow and border. Sizes are
// fractions of the picture's shorter side, so one style means the same on a
// small screenshot and on a 4K recording; each side turns them into pixels
// for the picture at hand.
//
// Recording-only choices (a blurred-video or image background) are not part
// of a style: applying one to a recording keeps them.
struct FrameStyle {
    enum class Background { Solid, LinearGradient, RadialGradient };

    Background background = Background::LinearGradient;
    QColor solidColor{0, 180, 219};
    QColor gradientStart{0, 180, 219};
    QColor gradientEnd{0, 131, 176};
    int gradientAngle = 135;

    // Fractions of the picture's shorter side. The defaults are the
    // BeautifySettings defaults at kReferenceShortSide.
    double padding = 64.0 / 1440.0;
    double cornerRadius = 12.0 / 1440.0;

    bool shadowEnabled = true;
    double shadowBlur = 40.0 / 1440.0;
    double shadowOffsetX = 0.0;
    double shadowOffsetY = 8.0 / 1440.0;
    QColor shadowColor{0, 0, 0, 80};

    bool borderEnabled = false;
    double borderWidth = 0.004;
    QColor borderColor{255, 255, 255, 170};

    BeautifyAspectRatio aspectRatio = BeautifyAspectRatio::Auto;

    // Reserved for a window or device frame (title bar, browser, laptop);
    // empty means none. Kept as is when a style is read and written.
    QString deviceFrame;

    // The picture size pixel styles from before #269 are converted at: their
    // look on a picture with this shorter side is unchanged.
    static constexpr int kReferenceShortSide = 1440;

    bool operator==(const FrameStyle& o) const;
    bool operator!=(const FrameStyle& o) const { return !(*this == o); }

    // Pixel values for a picture whose shorter side is shortSide, unclamped.
    BeautifySettings toBeautify(int shortSide) const;
    // The style of pixel settings made for a picture whose shorter side is shortSide.
    static FrameStyle fromBeautify(const BeautifySettings& settings, int shortSide = kReferenceShortSide);

    QJsonObject toJson() const;
    // Missing or unreadable keys keep the defaults; values are kept in range.
    static FrameStyle fromJson(const QJsonObject& json);
};

struct NamedFrameStyle {
    QString name;
    FrameStyle style;
};

} // namespace SnapTray
