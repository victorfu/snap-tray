#include <QtTest>

#include "recording/CursorProbe_mac.h"

#import <AppKit/AppKit.h>

using namespace SnapTray;

namespace {

struct Rgba {
    unsigned char r, g, b, a;
};

constexpr Rgba kRed{255, 0, 0, 255};
constexpr Rgba kGreen{0, 255, 0, 255};
constexpr Rgba kBlue{0, 0, 255, 255};
constexpr Rgba kClear{0, 0, 0, 0};

// One representation of `pixels` (row by row from the top) shown at `points`.
NSBitmapImageRep* makeRep(int width, int height, NSSize points, const QList<Rgba>& pixels)
{
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:0
                                                                  bitsPerPixel:0];
    rep.size = points;
    for (int y = 0; y < height; ++y) {
        unsigned char* row = rep.bitmapData + y * rep.bytesPerRow;
        for (int x = 0; x < width; ++x) {
            const Rgba& pixel = pixels[y * width + x];
            row[x * 4 + 0] = pixel.r;
            row[x * 4 + 1] = pixel.g;
            row[x * 4 + 2] = pixel.b;
            row[x * 4 + 3] = pixel.a;
        }
    }
    return rep;
}

// A 2 x 1 point image: green at 1x; at 2x red in the top-left pixel, clear
// in the bottom-right one and blue elsewhere.
NSImage* makeTwoScaleImage()
{
    const NSSize points = NSMakeSize(2, 1);
    NSImage* image = [[NSImage alloc] initWithSize:points];
    [image addRepresentation:makeRep(2, 1, points, {kGreen, kGreen})];
    [image addRepresentation:makeRep(4, 2, points,
                                     {kRed, kBlue, kBlue, kBlue, kBlue, kBlue, kBlue, kClear})];
    return image;
}

// Colour matching may move a primary slightly; it never swaps channels.
bool mostly(QRgb pixel, const Rgba& expected)
{
    constexpr int kTolerance = 60;
    return qAbs(qRed(pixel) - expected.r) <= kTolerance && qAbs(qGreen(pixel) - expected.g) <= kTolerance
           && qAbs(qBlue(pixel) - expected.b) <= kTolerance && qAbs(qAlpha(pixel) - expected.a) <= kTolerance;
}

} // namespace

class tst_CursorProbeMac : public QObject
{
    Q_OBJECT

private slots:
    void buttonNumbers();
    void buttonEventTypes();
    void eventAge();
    void imageAtRetinaScale();
    void imageAtStandardScale();
    void unusableImages();
    void probeReadsThePointerAndStops();
};

void tst_CursorProbeMac::buttonNumbers()
{
    QCOMPARE(MacCursor::buttonForNumber(0), std::optional<Qt::MouseButton>(Qt::LeftButton));
    QCOMPARE(MacCursor::buttonForNumber(1), std::optional<Qt::MouseButton>(Qt::RightButton));
    QCOMPARE(MacCursor::buttonForNumber(2), std::optional<Qt::MouseButton>(Qt::MiddleButton));
    QCOMPARE(MacCursor::buttonForNumber(3), std::optional<Qt::MouseButton>(Qt::BackButton));
    QCOMPARE(MacCursor::buttonForNumber(4), std::optional<Qt::MouseButton>(Qt::ForwardButton));
    QVERIFY(!MacCursor::buttonForNumber(5));
    QVERIFY(!MacCursor::buttonForNumber(-1));
}

void tst_CursorProbeMac::buttonEventTypes()
{
    QCOMPARE(MacCursor::pressForEventType(kCGEventLeftMouseDown), std::optional<bool>(true));
    QCOMPARE(MacCursor::pressForEventType(kCGEventLeftMouseUp), std::optional<bool>(false));
    QCOMPARE(MacCursor::pressForEventType(kCGEventRightMouseDown), std::optional<bool>(true));
    QCOMPARE(MacCursor::pressForEventType(kCGEventRightMouseUp), std::optional<bool>(false));
    QCOMPARE(MacCursor::pressForEventType(kCGEventOtherMouseDown), std::optional<bool>(true));
    QCOMPARE(MacCursor::pressForEventType(kCGEventOtherMouseUp), std::optional<bool>(false));
    // The event monitor delivers NSEvent types: the same numbers.
    QCOMPARE(MacCursor::pressForEventType(int(NSEventTypeLeftMouseDown)), std::optional<bool>(true));
    QCOMPARE(MacCursor::pressForEventType(int(NSEventTypeRightMouseUp)), std::optional<bool>(false));
    QCOMPARE(MacCursor::pressForEventType(int(NSEventTypeOtherMouseDown)), std::optional<bool>(true));
    QCOMPARE(MacCursor::pressForEventType(int(NSEventTypeOtherMouseUp)), std::optional<bool>(false));
    QVERIFY(!MacCursor::pressForEventType(kCGEventMouseMoved));
    QVERIFY(!MacCursor::pressForEventType(kCGEventLeftMouseDragged));
    QVERIFY(!MacCursor::pressForEventType(kCGEventScrollWheel));
    QVERIFY(!MacCursor::pressForEventType(kCGEventKeyDown));
    QVERIFY(!MacCursor::pressForEventType(int(kCGEventTapDisabledByTimeout)));
}

void tst_CursorProbeMac::eventAge()
{
    // Intel: a tick is a nanosecond, so there is only one kind of stamp.
    const MacCursor::UptimeClock intel{5000000000ULL, 1, 1};
    QCOMPARE(intel.nanoseconds(), quint64(5000000000ULL));
    QCOMPARE(MacCursor::eventAgeMs(intel, 4750000000ULL), qint64(250));
    QCOMPARE(MacCursor::eventAgeMs(intel, 5000000000ULL), qint64(0));
    // An event stamped after "now" (clock granularity) is not from the future.
    QCOMPARE(MacCursor::eventAgeMs(intel, 5001000000ULL), qint64(0));

    // Apple silicon: 125/3 ns per tick. 24e9 ticks are 1000 s.
    const MacCursor::UptimeClock arm{24000000000ULL, 125, 3};
    QCOMPARE(arm.nanoseconds(), quint64(1000000000000ULL));
    // A stamp in ticks, 6e6 ticks (250 ms) ago.
    QCOMPARE(MacCursor::eventAgeMs(arm, 24000000000ULL - 6000000ULL), qint64(250));
    // The same moment stamped in nanoseconds.
    QCOMPARE(MacCursor::eventAgeMs(arm, 1000000000000ULL - 250000000ULL), qint64(250));
    QCOMPARE(MacCursor::eventAgeMs(arm, 24000000000ULL), qint64(0));
    QCOMPARE(MacCursor::eventAgeMs(arm, 1000000000000ULL), qint64(0));
    // A nanosecond stamp slightly ahead of the clock.
    QCOMPARE(MacCursor::eventAgeMs(arm, 1000000000000ULL + 1000000ULL), qint64(0));
    // No overflow late in a long uptime (about 49 days of ticks).
    const MacCursor::UptimeClock late{100000000000000ULL, 125, 3};
    QCOMPARE(MacCursor::eventAgeMs(late, 100000000000000ULL - 24000000ULL), qint64(1000));
}

void tst_CursorProbeMac::imageAtRetinaScale()
{
    const auto shape = MacCursor::shapeFromImage(makeTwoScaleImage(), CGPointMake(1, 0.5), 2.0);
    QVERIFY(shape);
    // Pixel size, not point size, with the hotspot in the same pixels.
    QCOMPARE(shape->image.size(), QSize(4, 2));
    QCOMPARE(shape->image.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(shape->hotspot, QPoint(2, 1));
    // The 2x representation, the right way up.
    QVERIFY(mostly(shape->image.pixel(0, 0), kRed));
    QVERIFY(mostly(shape->image.pixel(1, 0), kBlue));
    QVERIFY(mostly(shape->image.pixel(0, 1), kBlue));
    QCOMPARE(qAlpha(shape->image.pixel(3, 1)), 0);
}

void tst_CursorProbeMac::imageAtStandardScale()
{
    const auto shape = MacCursor::shapeFromImage(makeTwoScaleImage(), CGPointMake(1, 0), 1.0);
    QVERIFY(shape);
    QCOMPARE(shape->image.size(), QSize(2, 1));
    QCOMPARE(shape->hotspot, QPoint(1, 0));
    // The 1x representation, not a scaled-down 2x one.
    QVERIFY(mostly(shape->image.pixel(0, 0), kGreen));
    QVERIFY(mostly(shape->image.pixel(1, 0), kGreen));
}

void tst_CursorProbeMac::unusableImages()
{
    QVERIFY(!MacCursor::shapeFromImage(nil, CGPointZero, 2.0));
    QVERIFY(!MacCursor::shapeFromImage(makeTwoScaleImage(), CGPointZero, 0.0));
    QVERIFY(!MacCursor::shapeFromImage([[NSImage alloc] initWithSize:NSZeroSize], CGPointZero, 2.0));
}

void tst_CursorProbeMac::probeReadsThePointerAndStops()
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) QSKIP("Requires the native Cocoa platform.");
    // Twice: the first probe's event source must be gone before the second starts.
    for (int round = 0; round < 2; ++round) {
        auto probe = createCursorProbe();
        QVERIFY(probe);
        QVERIFY(probe->name().startsWith(QLatin1String("macos-")));
        QVERIFY(probe->capabilities().shapes);
        // Clicks come from the tap or, without Input Monitoring (as on a CI
        // runner), from the monitor; either way they are available.
        QVERIFY(probe->capabilities().clicks);
        QVERIFY(probe->name() == QLatin1String("macos-event-tap")
                || probe->name() == QLatin1String("macos-event-monitor"));
        const auto state = probe->poll();
        QVERIFY(state);
        QVERIFY(!state->physical);
        QVERIFY(state->shapeSerial != 0);
        // The session may not have a readable pointer (unknown shape); when
        // it does, the image is well-formed.
        if (const auto shape = probe->currentShape()) {
            QVERIFY(!shape->image.isNull());
            QCOMPARE(shape->image.format(), QImage::Format_ARGB32_Premultiplied);
            QVERIFY(shape->image.rect().contains(shape->hotspot));
        }
        // Later polls re-read the shape without disturbing the state.
        QTest::qWait(MacCursor::kShapePollIntervalMs * 2);
        QVERIFY(probe->poll());
    }
}

QTEST_MAIN(tst_CursorProbeMac)
#include "tst_CursorProbe_mac.moc"
