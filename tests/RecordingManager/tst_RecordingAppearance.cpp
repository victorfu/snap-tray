#include <QtTest>

#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

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
    void costAt4K();
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

QTEST_MAIN(TestRecordingAppearance)
#include "tst_RecordingAppearance.moc"
