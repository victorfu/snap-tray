#include <QtTest>

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

#include "beautify/BeautifyRenderer.h"
#include "recording/RecordingAppearance.h"

using namespace SnapTray;

namespace {

QImage filled(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

bool near(const QColor& a, const QColor& b, int tolerance = 12)
{
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
        && qAbs(a.blue() - b.blue()) <= tolerance;
}

RecordingAppearance solidWhite()
{
    RecordingAppearance a;
    a.enabled = true;
    a.background = RecordingAppearance::Background::Solid;
    a.solidColor = Qt::white;
    a.shadow = false;
    a.corners = RecordingAppearance::Amount::None;
    return a;
}

} // namespace

class TestRecordingAppearance : public QObject
{
    Q_OBJECT

private slots:
    void disabledIsIdentity();
    void sizesFollowThePicture();
    void composePlacesThePicture();
    void roundedCornersShowTheBackground();
    void borderIsInsideThePicture();
    void blurredBackgroundComesFromTheGivenSource();
    void imageBackgroundAndFallback();
    void previewLeavesThePictureClear();
    void jsonRoundTrip();
    void fromBeautify();
    void translucentColorsRenderOpaque();
    void opaqueAndAlphaFormatsRenderIdentically();
    void costAt4K();
    // Shared styles (#269)
    void oldLookIsUnchanged();
    void styleRoundTrip();
    void styleKeepsRecordingBackground();
    void customValuesJsonRoundTrip();
    void inactiveCustomValuesIgnored();
    void sameStyleMatchesScreenshot();
};

void TestRecordingAppearance::disabledIsIdentity()
{
    RecordingAppearance a;
    QVERIFY(!a.enabled);
    QCOMPARE(a.outputSize(QSize(640, 360)), QSize(640, 360));
    QCOMPARE(a.contentRect(QSize(640, 360)), QRect(0, 0, 640, 360));
    const QImage content = filled(QSize(64, 36), Qt::red);
    QCOMPARE(RecordingAppearanceRenderer(a, content.size()).compose(content), content);
}

void TestRecordingAppearance::sizesFollowThePicture()
{
    RecordingAppearance a = solidWhite();
    a.padding = RecordingAppearance::Amount::Medium; // 8 % of the shorter side
    QCOMPARE(a.outputSize(QSize(1000, 500)), QSize(1080, 580));
    QCOMPARE(a.contentRect(QSize(1000, 500)), QRect(40, 40, 1000, 500));
    // Relative: twice the picture, twice the frame.
    QCOMPARE(a.outputSize(QSize(2000, 1000)), QSize(2160, 1160));
    // Even sides, never smaller than the picture.
    a.padding = RecordingAppearance::Amount::Small;
    const QSize odd = a.outputSize(QSize(333, 211));
    QCOMPARE(odd.width() % 2, 0);
    QCOMPARE(odd.height() % 2, 0);
    QVERIFY(odd.width() >= 333 && odd.height() >= 211);
    // An aspect ratio grows the frame to that shape, centred.
    a.padding = RecordingAppearance::Amount::None;
    a.aspectRatio = BeautifyAspectRatio::Square_1_1;
    QCOMPARE(a.outputSize(QSize(1000, 500)), QSize(1000, 1000));
    QCOMPARE(a.contentRect(QSize(1000, 500)), QRect(0, 250, 1000, 500));
}

void TestRecordingAppearance::composePlacesThePicture()
{
    RecordingAppearance a = solidWhite();
    const QSize size(200, 100);
    const RecordingAppearanceRenderer renderer(a, size);
    const QImage out = renderer.compose(filled(size, Qt::red));
    QCOMPARE(out.size(), a.outputSize(size));
    const QRect inset = renderer.contentRect();
    QCOMPARE(out.pixelColor(2, 2), QColor(Qt::white));
    QCOMPARE(out.pixelColor(inset.center()), QColor(Qt::red));
    QCOMPARE(out.pixelColor(inset.topLeft()), QColor(Qt::red)); // square corners
    QCOMPARE(out.pixelColor(inset.left() - 1, inset.center().y()), QColor(Qt::white));
    // Same input, same frame.
    QCOMPARE(renderer.compose(filled(size, Qt::red)), out);
}

void TestRecordingAppearance::roundedCornersShowTheBackground()
{
    RecordingAppearance a = solidWhite();
    a.corners = RecordingAppearance::Amount::Large;
    const QSize size(400, 200);
    const RecordingAppearanceRenderer renderer(a, size);
    const QImage out = renderer.compose(filled(size, Qt::red));
    const QRect inset = renderer.contentRect();
    QVERIFY(a.cornerRadius(size) >= 10);
    QCOMPARE(out.pixelColor(inset.topLeft() + QPoint(1, 1)), QColor(Qt::white));
    QCOMPARE(out.pixelColor(inset.bottomRight() - QPoint(1, 1)), QColor(Qt::white));
    QCOMPARE(out.pixelColor(inset.center()), QColor(Qt::red));
    QCOMPARE(out.pixelColor(inset.left() + 1, inset.center().y()), QColor(Qt::red));
}

void TestRecordingAppearance::borderIsInsideThePicture()
{
    RecordingAppearance a = solidWhite();
    a.background = RecordingAppearance::Background::Solid;
    a.solidColor = Qt::black;
    a.border = true;
    const QSize size(400, 200);
    const RecordingAppearanceRenderer renderer(a, size);
    const QImage out = renderer.compose(filled(size, Qt::blue));
    const QRect inset = renderer.contentRect();
    QCOMPARE(out.size(), a.outputSize(size)); // the border does not grow the frame
    const QColor edge = out.pixelColor(inset.center().x(), inset.top());
    QVERIFY2(edge.red() > 100 && edge.green() > 100, qPrintable(edge.name())); // light border over blue
    QCOMPARE(out.pixelColor(inset.center()), QColor(Qt::blue));
    QCOMPARE(out.pixelColor(inset.center().x(), inset.top() - 2), QColor(Qt::black));
}

void TestRecordingAppearance::blurredBackgroundComesFromTheGivenSource()
{
    RecordingAppearance a = solidWhite();
    a.background = RecordingAppearance::Background::BlurredVideo;
    a.padding = RecordingAppearance::Amount::Large;
    const QSize size(480, 240);
    const RecordingAppearanceRenderer renderer(a, size);
    // The picture shows a secret in red; its masked version is all green.
    const QImage picture = filled(size, Qt::red);
    const QImage masked = filled(size, Qt::green);
    const QImage out = renderer.compose(picture, RecordingAppearanceRenderer::blurSourceFor(masked));
    const QColor background = out.pixelColor(3, 3);
    QVERIFY2(background.green() > 120 && background.red() < 40, qPrintable(background.name()));
    // Without a separate source the picture itself is used.
    const QColor fromPicture = renderer.compose(picture).pixelColor(3, 3);
    QVERIFY2(fromPicture.red() > 120 && fromPicture.green() < 40, qPrintable(fromPicture.name()));
}

// PR #198 review: screenshot Beautify allows a translucent solid color. The
// frame takes it opaque, and any background is drawn over an opaque base, so
// the padding never shows uninitialized pixels.
void TestRecordingAppearance::translucentColorsRenderOpaque()
{
    BeautifySettings beautify;
    beautify.backgroundType = BeautifyBackgroundType::Solid;
    beautify.backgroundColor = QColor(200, 40, 40, 60);
    const RecordingAppearance imported = RecordingAppearance::fromBeautify(beautify);
    QCOMPARE(imported.solidColor.alpha(), 255);
    QCOMPARE(imported.solidColor.rgb(), QColor(200, 40, 40).rgb());

    RecordingAppearance clear = solidWhite();
    clear.solidColor = QColor(0, 0, 0, 0); // set directly, not through the importers
    const QSize size(200, 100);
    for (int run = 0; run < 3; ++run) {
        const QImage out = RecordingAppearanceRenderer(clear, size).compose(filled(size, Qt::white));
        QCOMPARE(out.pixelColor(2, 2), QColor(Qt::black));
        QCOMPARE(out.pixelColor(out.width() - 3, out.height() - 3), QColor(Qt::black));
    }
}

void TestRecordingAppearance::imageBackgroundAndFallback()
{
    RecordingAppearance a = solidWhite();
    a.background = RecordingAppearance::Background::Image;
    const QSize size(200, 100);
    const QImage out = RecordingAppearanceRenderer(a, size, filled(QSize(50, 50), Qt::magenta))
                           .compose(filled(size, Qt::black));
    QCOMPARE(out.pixelColor(2, 2), QColor(Qt::magenta));
    // A transparent image (PR #198 review): an opaque, deterministic base
    // shows through, never uninitialized memory.
    QImage clear(QSize(50, 50), QImage::Format_ARGB32);
    clear.fill(Qt::transparent);
    for (int run = 0; run < 3; ++run) {
        const QImage overClear = RecordingAppearanceRenderer(a, size, clear).compose(filled(size, Qt::white));
        QCOMPARE(overClear.pixelColor(2, 2), QColor(Qt::black));
        QCOMPARE(overClear.pixelColor(overClear.width() - 3, overClear.height() - 3), QColor(Qt::black));
    }
    // No image: the gradient, never an empty frame.
    const QImage fallback = RecordingAppearanceRenderer(a, size).compose(filled(size, Qt::black));
    const auto preset = beautifyPresets().front();
    QVERIFY(fallback.pixelColor(2, 2) != QColor(Qt::black));
    QVERIFY(near(fallback.pixelColor(2, 2), preset.gradientStart, 60) || near(fallback.pixelColor(2, 2), preset.gradientEnd, 60));
}

void TestRecordingAppearance::previewLeavesThePictureClear()
{
    RecordingAppearance a = solidWhite();
    a.corners = RecordingAppearance::Amount::Medium;
    const QSize size(400, 200);
    const RecordingAppearanceRenderer exporter(a, size);
    const QImage exported = exporter.compose(filled(size, Qt::red));
    // The preview at half size: the same geometry, scaled.
    const RecordingAppearanceRenderer preview(a, size, QImage(), false);
    QImage view(exported.size() / 2, QImage::Format_ARGB32_Premultiplied);
    view.fill(Qt::transparent);
    QPainter painter(&view);
    const QRect inset = exporter.contentRect();
    preview.paintAround(painter, QRectF(QPointF(inset.topLeft()) / 2, QSizeF(inset.size()) / 2));
    painter.end();
    QCOMPARE(qAlpha(view.pixel(inset.center() / 2)), 0);          // the video shows through
    QCOMPARE(view.pixelColor(1, 1), QColor(Qt::white));            // the frame around it
    // The rounded corner is covered (anti-aliasing may leave it a little translucent).
    QVERIFY(qAlpha(view.pixel(inset.topLeft() / 2)) > 200);
}

void TestRecordingAppearance::jsonRoundTrip()
{
    RecordingAppearance a;
    a.enabled = true;
    a.background = RecordingAppearance::Background::Image;
    a.gradientPreset = 3;
    a.solidColor = QColor(10, 20, 30);
    a.imagePath = QStringLiteral("/tmp/bg.png");
    a.padding = RecordingAppearance::Amount::Large;
    a.corners = RecordingAppearance::Amount::Small;
    a.shadow = false;
    a.border = true;
    a.aspectRatio = BeautifyAspectRatio::Tall_9_16;
    QCOMPARE(RecordingAppearance::fromJson(a.toJson()), a);
    const auto odd = RecordingAppearance::fromJson({{QStringLiteral("background"), QStringLiteral("video")},
                                                    {QStringLiteral("gradientPreset"), 99},
                                                    {QStringLiteral("aspectRatio"), 42},
                                                    {QStringLiteral("solidColor"), QStringLiteral("nope")}});
    const RecordingAppearance defaults;
    QCOMPARE(odd, defaults);
}

void TestRecordingAppearance::fromBeautify()
{
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = QColor(1, 2, 3);
    settings.shadowEnabled = false;
    settings.cornerRadius = 0;
    settings.aspectRatio = BeautifyAspectRatio::Wide_16_9;
    const auto a = RecordingAppearance::fromBeautify(settings);
    QVERIFY(!a.enabled); // never switched on for the user
    QCOMPARE(a.background, RecordingAppearance::Background::Solid);
    QCOMPARE(a.solidColor, QColor(1, 2, 3));
    QVERIFY(!a.shadow);
    QCOMPARE(a.corners, RecordingAppearance::Amount::None);
    QCOMPARE(a.aspectRatio, BeautifyAspectRatio::Wide_16_9);
}

void TestRecordingAppearance::opaqueAndAlphaFormatsRenderIdentically()
{
    const QSize size(320, 180);
    QImage picture = filled(size, Qt::darkCyan);
    QPainter painter(&picture);
    painter.fillRect(QRect(20, 25, 100, 60), Qt::yellow);
    painter.end();
    const QImage alphaPicture = picture.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage maskedBlur = RecordingAppearanceRenderer::blurSourceFor(filled(size, Qt::green));
    for (auto corners : {RecordingAppearance::Amount::None, RecordingAppearance::Amount::Large}) {
        RecordingAppearance appearance;
        appearance.enabled = true;
        appearance.background = RecordingAppearance::Background::BlurredVideo;
        appearance.padding = RecordingAppearance::Amount::Medium;
        appearance.corners = corners;
        appearance.shadow = true;
        appearance.border = true;
        const RecordingAppearanceRenderer renderer(appearance, size);
        // Identical opaque pixels, but the alpha format must retain the full
        // background rendering path used by genuinely translucent inputs.
        QCOMPARE(renderer.compose(picture, maskedBlur), renderer.compose(alphaPicture, maskedBlur));
    }
}

void TestRecordingAppearance::costAt4K()
{
    const QSize size(3840, 2160);
    const QImage content = filled(size, Qt::darkCyan);
    for (const auto background : {RecordingAppearance::Background::Gradient,
                                  RecordingAppearance::Background::BlurredVideo}) {
        RecordingAppearance a;
        a.enabled = true;
        a.background = background;
        a.padding = RecordingAppearance::Amount::Medium;
        a.corners = RecordingAppearance::Amount::Medium;
        a.shadow = true;
        a.border = true;
        QElapsedTimer setup;
        setup.start();
        const RecordingAppearanceRenderer renderer(a, size);
        const double setupMs = setup.nsecsElapsed() / 1e6;
        constexpr int kFrames = 10;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < kFrames; ++i) {
            const QImage blur = background == RecordingAppearance::Background::BlurredVideo
                ? RecordingAppearanceRenderer::blurSourceFor(content) : QImage();
            const QImage out = renderer.compose(content, blur);
            QVERIFY(!out.isNull());
        }
        const double perFrame = timer.nsecsElapsed() / 1e6 / kFrames;
        qInfo("frame around a 4K picture (%s): %.1f ms setup, %.1f ms per frame",
              background == RecordingAppearance::Background::Gradient ? "gradient" : "blurred video", setupMs,
              perFrame);
        // Next to decoding and encoding a 4K frame; loose for slow CI.
        QVERIFY(perFrame < 250.0);
    }
}

namespace {

// A style that uses everything the presets cannot express.
FrameStyle customStyle()
{
    FrameStyle style;
    style.background = FrameStyle::Background::LinearGradient;
    style.gradientStart = QColor(250, 120, 30);
    style.gradientEnd = QColor(40, 20, 160);
    style.gradientAngle = 45;
    style.padding = 0.05;
    style.cornerRadius = 0.025;
    style.shadowEnabled = true;
    style.shadowBlur = 0.03;
    style.shadowOffsetX = 0.01;
    style.shadowOffsetY = 0.02;
    style.shadowColor = QColor(10, 0, 40, 110);
    style.borderEnabled = true;
    style.borderWidth = 0.01;
    style.borderColor = QColor(255, 255, 255, 200);
    style.aspectRatio = BeautifyAspectRatio::Auto;
    return style;
}

} // namespace

void TestRecordingAppearance::oldLookIsUnchanged()
{
    // A frame saved before #269 (only the old keys) keeps its pixel values.
    const QJsonObject old{{QStringLiteral("enabled"), true},
                          {QStringLiteral("background"), QStringLiteral("gradient")},
                          {QStringLiteral("gradientPreset"), 2},
                          {QStringLiteral("solidColor"), QStringLiteral("#ffffff")},
                          {QStringLiteral("imagePath"), QString()},
                          {QStringLiteral("padding"), QStringLiteral("medium")},
                          {QStringLiteral("corners"), QStringLiteral("large")},
                          {QStringLiteral("shadow"), true},
                          {QStringLiteral("border"), true},
                          {QStringLiteral("aspectRatio"), 0}};
    const auto a = RecordingAppearance::fromJson(old);
    const QSize content(1920, 1080);
    const BeautifySettings s = a.beautifyFor(content);
    const BeautifyPreset preset = beautifyPresets().at(2);
    QCOMPARE(s.backgroundType, BeautifyBackgroundType::LinearGradient);
    QCOMPARE(s.gradientStartColor, preset.gradientStart);
    QCOMPARE(s.gradientEndColor, preset.gradientEnd);
    QCOMPARE(s.gradientAngle, preset.gradientAngle);
    QCOMPARE(s.padding, 86);      // round(1080 * 0.08)
    QCOMPARE(s.cornerRadius, 65); // round(1080 * 0.06)
    QCOMPARE(s.shadowBlur, 43);   // round(1080 * 0.04)
    QCOMPARE(s.shadowOffsetX, 0);
    QCOMPARE(s.shadowOffsetY, 11); // round(1080 * 0.01)
    QCOMPARE(s.shadowColor, QColor(0, 0, 0, 80));
    QCOMPARE(a.borderWidth(content), 4); // round(1080 * 0.004)
    QCOMPARE(a.outputSize(content), QSize(2092, 1252));
    // And it reads back the same after a save in this version.
    QCOMPARE(RecordingAppearance::fromJson(a.toJson()), a);
}

void TestRecordingAppearance::styleRoundTrip()
{
    RecordingAppearance a;
    a.applyFrameStyle(customStyle());
    QVERIFY(a.enabled);
    QCOMPARE(a.background, RecordingAppearance::Background::Gradient);
    QCOMPARE(a.gradientPreset, RecordingAppearance::kCustomGradient);
    QCOMPARE(a.padding, RecordingAppearance::Amount::Custom);
    QCOMPARE(a.corners, RecordingAppearance::Amount::Custom);
    QCOMPARE(a.frameStyle(), customStyle());

    // Preset sizes and gradients are recognised as the presets.
    FrameStyle presetLike;
    presetLike.gradientStart = beautifyPresets().at(4).gradientStart;
    presetLike.gradientEnd = beautifyPresets().at(4).gradientEnd;
    presetLike.gradientAngle = beautifyPresets().at(4).gradientAngle;
    presetLike.padding = RecordingAppearance::kPaddingFractions[1];
    presetLike.cornerRadius = RecordingAppearance::kCornerFractions[3];
    a.applyFrameStyle(presetLike);
    QCOMPARE(a.gradientPreset, 4);
    QCOMPARE(a.padding, RecordingAppearance::Amount::Small);
    QCOMPARE(a.corners, RecordingAppearance::Amount::Large);
    QCOMPARE(a.frameStyle(), presetLike);

    // Radial and solid backgrounds.
    FrameStyle radial = customStyle();
    radial.background = FrameStyle::Background::RadialGradient;
    a.applyFrameStyle(radial);
    QCOMPARE(a.beautifyFor(QSize(800, 600)).backgroundType, BeautifyBackgroundType::RadialGradient);
    FrameStyle solid = customStyle();
    solid.background = FrameStyle::Background::Solid;
    solid.solidColor = QColor(9, 99, 199);
    a.applyFrameStyle(solid);
    QCOMPARE(a.background, RecordingAppearance::Background::Solid);
    QCOMPARE(a.frameStyle(), solid);
}

void TestRecordingAppearance::styleKeepsRecordingBackground()
{
    // Acceptance (#269): recording-only choices survive applying a style.
    for (const auto background : {RecordingAppearance::Background::BlurredVideo,
                                  RecordingAppearance::Background::Image}) {
        RecordingAppearance a;
        a.enabled = true;
        a.background = background;
        a.imagePath = QStringLiteral("/tmp/bg.png");
        a.applyFrameStyle(customStyle());
        QCOMPARE(a.background, background);
        QCOMPARE(a.imagePath, QStringLiteral("/tmp/bg.png"));
        // The rest of the look is the style's, with its gradient underneath.
        QCOMPARE(a.frameStyle(), customStyle());
    }

    // Solid and radial styles also come back whole from beneath them, and a
    // missing image shows the style's own background.
    FrameStyle solid = customStyle();
    solid.background = FrameStyle::Background::Solid;
    solid.solidColor = QColor(9, 99, 199);
    FrameStyle radial = customStyle();
    radial.background = FrameStyle::Background::RadialGradient;
    for (const auto background : {RecordingAppearance::Background::BlurredVideo,
                                  RecordingAppearance::Background::Image}) {
        for (const FrameStyle& style : {solid, radial}) {
            RecordingAppearance a;
            a.enabled = true;
            a.background = background;
            a.applyFrameStyle(style);
            QCOMPARE(a.background, background);
            QCOMPARE(a.frameStyle(), style);
            QCOMPARE(RecordingAppearance::fromJson(a.toJson()).frameStyle(), style);
        }
    }
    RecordingAppearance missingImage;
    missingImage.enabled = true;
    missingImage.background = RecordingAppearance::Background::Image;
    missingImage.applyFrameStyle(solid);
    QCOMPARE(missingImage.beautifyFor(QSize(800, 600)).backgroundType, BeautifyBackgroundType::Solid);
    // Nothing drawn over the solid color: nothing beneath it to remember.
    missingImage.background = RecordingAppearance::Background::Solid;
    RecordingAppearance plain = missingImage;
    plain.solidUnderneath = false;
    QCOMPARE(missingImage, plain);

    // A style saved from a tiny screenshot stays within the recording's range.
    FrameStyle wide = customStyle();
    wide.padding = 1.6;
    wide.shadowOffsetY = -3.0;
    RecordingAppearance a;
    a.applyFrameStyle(wide);
    QCOMPARE(a.customPadding, 1.0);
    QCOMPARE(a.shadowOffsetY, -1.0);
    QCOMPARE(RecordingAppearance::fromJson(a.toJson()), a);
}

void TestRecordingAppearance::inactiveCustomValuesIgnored()
{
    // A custom size or gradient left behind by choosing a preset neither
    // makes two identical frames differ nor reaches the saved JSON.
    RecordingAppearance a;
    a.applyFrameStyle(customStyle());
    a.padding = RecordingAppearance::Amount::Large;
    a.corners = RecordingAppearance::Amount::Small;
    a.gradientPreset = 3;
    RecordingAppearance b;
    b.applyFrameStyle(customStyle());
    b.padding = RecordingAppearance::Amount::Large;
    b.corners = RecordingAppearance::Amount::Small;
    b.gradientPreset = 3;
    b.customPadding = 0.5;
    b.gradientStart = QColor(1, 2, 3);
    b.gradientRadial = true;
    QCOMPARE(a, b);
    QCOMPARE(a.toJson(), b.toJson());
    const QJsonObject json = b.toJson();
    QCOMPARE(json.value(QStringLiteral("customPadding")).toDouble(), 0.0);
    QCOMPARE(json.value(QStringLiteral("gradientRadial")).toBool(), false);

    b.dropInactiveCustomValues();
    QCOMPARE(b.customPadding, 0.0);
    QCOMPARE(b.gradientRadial, false);
    QCOMPARE(b.gradientStart, RecordingAppearance().gradientStart);

    // Values in use are kept.
    RecordingAppearance c;
    c.applyFrameStyle(customStyle());
    const RecordingAppearance before = c;
    c.dropInactiveCustomValues();
    QCOMPARE(c.customPadding, before.customPadding);
    QCOMPARE(c.gradientStart, before.gradientStart);
    QCOMPARE(c.frameStyle(), customStyle());
}

void TestRecordingAppearance::customValuesJsonRoundTrip()
{
    RecordingAppearance a;
    a.applyFrameStyle(customStyle());
    QCOMPARE(RecordingAppearance::fromJson(a.toJson()), a);
    const auto wild = RecordingAppearance::fromJson({{QStringLiteral("padding"), QStringLiteral("custom")},
                                                     {QStringLiteral("customPadding"), 50.0},
                                                     {QStringLiteral("gradientPreset"), -1},
                                                     {QStringLiteral("gradientStart"), QStringLiteral("#80ff0000")}});
    QCOMPARE(wild.padding, RecordingAppearance::Amount::Custom);
    QCOMPARE(wild.customPadding, 1.0); // never more than the picture
    QCOMPARE(wild.gradientPreset, RecordingAppearance::kCustomGradient);
    QCOMPARE(wild.gradientStart.alpha(), 255); // a video frame has nothing behind it
}

void TestRecordingAppearance::sameStyleMatchesScreenshot()
{
    // Acceptance (#269): one style on a screenshot and on a recording frame
    // gives the same background, padding, corners, shadow and border where
    // both use the same pixels. Screenshot Beautify uses a style's pixels at
    // FrameStyle::kReferenceShortSide; the recording frame scales with the
    // video, so the two meet on a picture with that shorter side.
    // Allowed: kChannelTolerance per channel on at most kMaxDifferingShare of
    // the pixels (anti-aliased edges are drawn through different image
    // formats), and the same size exactly.
    constexpr int kChannelTolerance = 2;
    constexpr double kMaxDifferingShare = 0.001;
    const QSize size(1600, FrameStyle::kReferenceShortSide);
    QImage picture(size, QImage::Format_RGB32);
    picture.fill(QColor(30, 160, 220));
    QPainter painter(&picture);
    painter.fillRect(QRect(160, 120, 800, 480), QColor(240, 200, 40));
    painter.end();

    for (const auto background : {FrameStyle::Background::LinearGradient, FrameStyle::Background::RadialGradient,
                                  FrameStyle::Background::Solid}) {
        FrameStyle style = customStyle();
        style.background = background;
        style.solidColor = QColor(70, 80, 90);

        const QImage screenshot = BeautifyRenderer::applyToPixmap(QPixmap::fromImage(picture),
                                                                  style.toBeautify(FrameStyle::kReferenceShortSide))
                                      .toImage()
                                      .convertToFormat(QImage::Format_RGB32);
        RecordingAppearance a;
        a.applyFrameStyle(style);
        const QImage frame = RecordingAppearanceRenderer(a, size).compose(picture).convertToFormat(QImage::Format_RGB32);
        QCOMPARE(frame.size(), screenshot.size());

        qint64 differing = 0;
        int worst = 0;
        for (int y = 0; y < frame.height(); ++y) {
            const auto* f = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
            const auto* s = reinterpret_cast<const QRgb*>(screenshot.constScanLine(y));
            for (int x = 0; x < frame.width(); ++x) {
                const int d = std::max({qAbs(qRed(f[x]) - qRed(s[x])), qAbs(qGreen(f[x]) - qGreen(s[x])),
                                        qAbs(qBlue(f[x]) - qBlue(s[x]))});
                worst = std::max(worst, d);
                if (d > kChannelTolerance) ++differing;
            }
        }
        const double share = double(differing) / (frame.width() * frame.height());
        qInfo("background %d: worst channel difference %d, %.4f%% of pixels beyond %d", int(background), worst,
              share * 100.0, kChannelTolerance);
        QVERIFY2(share <= kMaxDifferingShare, qPrintable(QStringLiteral("%1 pixels differ").arg(differing)));
    }
}

QTEST_MAIN(TestRecordingAppearance)
#include "tst_RecordingAppearance.moc"
