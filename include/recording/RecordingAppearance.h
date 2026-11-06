#pragma once

#include "beautify/BeautifySettings.h"
#include "beautify/FrameStyle.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <QString>

class QPainter;

// The frame around an exported recording (#184): a background, space around
// the picture, rounded corners, a shadow and a border, drawn with the
// screenshot Beautify rules (BeautifyRenderer). Sizes are fractions of the
// picture's shorter side, so a look means the same at any resolution.
//
// Order in every export (and the preview): masks, zoom, pointer on the
// picture, then this frame around it, then any MP4 resize. Nothing drawn
// here is taken from unmasked pixels: the blurred background is made from
// the picture after its masks.
//
// A saved style shared with screenshot Beautify (FrameStyle, #269) can set
// any look; values between the presets show as Custom. The blurred-video
// and image backgrounds belong to recordings only and are kept when a style
// is applied.
namespace SnapTray {

struct RecordingAppearance {
    enum class Background { Gradient, Solid, BlurredVideo, Image };
    // Padding and corner presets; Custom uses customPadding / customCorners.
    enum class Amount { None, Small, Medium, Large, Custom };
    // gradientPreset value for the gradient in gradientStart/End/Angle/Radial.
    static constexpr int kCustomGradient = -1;

    bool enabled = false;
    Background background = Background::Gradient;
    int gradientPreset = 0;          // index into beautifyPresets(), or kCustomGradient
    QColor gradientStart{0, 180, 219};
    QColor gradientEnd{0, 131, 176};
    int gradientAngle = 135;
    bool gradientRadial = false;
    QColor solidColor{Qt::white};
    QString imagePath;               // Background::Image
    Amount padding = Amount::Medium;
    Amount corners = Amount::Medium;
    double customPadding = 0.0;      // fractions of the shorter side, for Amount::Custom
    double customCorners = 0.0;
    bool shadow = true;
    double shadowBlur = kShadowBlurFraction;
    double shadowOffsetX = 0.0;
    double shadowOffsetY = kShadowOffsetFraction;
    QColor shadowColor{0, 0, 0, 80};
    bool border = false;
    double borderFraction = kBorderFraction;
    QColor borderColor{255, 255, 255, 170};
    BeautifyAspectRatio aspectRatio = BeautifyAspectRatio::Auto;

    bool operator==(const RecordingAppearance& o) const;
    bool operator!=(const RecordingAppearance& o) const { return !(*this == o); }

    QJsonObject toJson() const;
    static RecordingAppearance fromJson(const QJsonObject& json); // missing or unknown keys keep defaults
    // A first look taken from the screenshot Beautify settings.
    static RecordingAppearance fromBeautify(const BeautifySettings& settings);

    // The look as a shared style. With a blurred-video or image background
    // the style carries the gradient underneath.
    FrameStyle frameStyle() const;
    // Takes a shared style's look and switches the frame on. A blurred-video
    // or image background stays; the style's background is kept beneath it.
    void applyFrameStyle(const FrameStyle& style);

    // Pixel values for a picture of this size, in BeautifySettings terms.
    BeautifySettings beautifyFor(const QSize& contentSize) const;
    // The framed output (even sides, never smaller than the picture) and
    // where the picture sits in it. Identity when not enabled.
    QSize outputSize(const QSize& contentSize) const;
    QRect contentRect(const QSize& contentSize) const;
    int borderWidth(const QSize& contentSize) const;
    double cornerRadius(const QSize& contentSize) const;

    static constexpr double kPaddingFractions[] = {0.0, 0.04, 0.08, 0.14};
    static constexpr double kCornerFractions[] = {0.0, 0.015, 0.03, 0.06};
    static constexpr double kShadowBlurFraction = 0.04;
    static constexpr double kShadowOffsetFraction = 0.01;
    static constexpr double kBorderFraction = 0.004;
    static const QColor kBorderColor;

private:
    double paddingFraction() const;
    double cornerFraction() const;
};

// Draws the frame for one picture size. Keeps the parts that do not change
// from frame to frame (background without the video, shadow, corner mask),
// so a frame costs one copy and one masked draw. Not thread-safe; give each
// export its own.
class RecordingAppearanceRenderer {
public:
    // backgroundImage: the decoded image for Background::Image (a missing one
    // falls back to the gradient).
    // cacheLayers: keep the static layers for compose(); the preview's
    // paintAround() draws without them, at the view's resolution.
    RecordingAppearanceRenderer(const RecordingAppearance& appearance, const QSize& contentSize,
                                const QImage& backgroundImage = QImage(), bool cacheLayers = true);

    QSize outputSize() const { return m_outputSize; }
    QRect contentRect() const { return m_contentRect; }
    // The framed frame for `content` (the picture at contentSize, already
    // masked, zoomed and with the pointer). The blurred background is made
    // from `blurSource` (the masked picture), or from `content` when null.
    QImage compose(const QImage& content, const QImage& blurSource = QImage()) const;
    // A small copy of a (masked) picture to make the blurred background from;
    // what compose() and paintAround() take as blurSource.
    static QImage blurSourceFor(const QImage& picture);
    // The preview: everything but the picture, over a view where the picture
    // fills `contentInView`; the picture's rounded shape is left clear.
    void paintAround(QPainter& painter, const QRectF& contentInView, const QImage& blurSource = QImage()) const;

private:
    void drawBackground(QPainter& painter, const QImage& blurSource) const;
    void drawDecorations(QPainter& painter) const; // shadow under the picture
    void drawBorder(QPainter& painter) const;

    RecordingAppearance m_appearance;
    BeautifySettings m_settings;
    QSize m_contentSize;
    QSize m_outputSize;
    QRect m_contentRect;
    QImage m_backgroundImage;
    QImage m_staticLayer; // background and shadow, when the background does not move
    QImage m_cornerMask;  // the picture's rounded shape, white on transparent
};

} // namespace SnapTray
