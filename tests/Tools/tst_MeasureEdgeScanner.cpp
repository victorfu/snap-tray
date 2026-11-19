#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QImage>
#include <QRandomGenerator>

#include "tools/MeasureEdgeScanner.h"

using MeasureEdgeScanner::Span;

// Fixed, generated test images for the Measure tool's edge finder (#348):
// solid, low contrast, gradient, shadow, anti-aliasing, noise, a 1px line and
// blank space. Expected values are the pixels a person would count.
class TestMeasureEdgeScanner : public QObject
{
    Q_OBJECT

private slots:
    void solidElement();
    void gapBetweenElements();
    void lowContrastEdge();
    void gradientAloneHasNoEdge_data();
    void gradientAloneHasNoEdge();
    void elementOnGradient();
    void softShadowIsNotAnEdge();
    void antiAliasedEdgeGoesToTheSideCoveringMost();
    void noisyBackground();
    void hairline();
    void blankImageIsNotReliable();
    void nearbyEdgesUsePixelBoundaries();
    void nearbyEdgeRejectsEasedGradient_data();
    void nearbyEdgeRejectsEasedGradient();
    void transparentStripIsAnEdge();
    void originOutsideIsClampedAndOtherFormatsRefused();
    void largeImageHoverIsFast();
};

namespace {

QImage filled(int width, int height, QColor color)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

void fillRect(QImage& image, const QRect& rect, QColor color)
{
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            image.setPixelColor(x, y, color);
        }
    }
}

QColor gray(int value)
{
    return QColor(value, value, value);
}

Span scanned(const QImage& image, QPoint origin, Qt::Orientation orientation)
{
    const std::optional<Span> span = MeasureEdgeScanner::scan(image, origin, orientation);
    return span.value_or(Span{});
}

void verifySpan(const Span& span, int first, int last, bool firstIsEdge, bool lastIsEdge)
{
    QCOMPARE(span.first, first);
    QCOMPARE(span.last, last);
    QCOMPARE(span.firstIsEdge, firstIsEdge);
    QCOMPARE(span.lastIsEdge, lastIsEdge);
}

} // namespace

// A button: its width and height, wherever inside it the pointer is.
void TestMeasureEdgeScanner::solidElement()
{
    QImage image = filled(200, 100, Qt::white);
    fillRect(image, QRect(QPoint(40, 20), QPoint(119, 59)), QColor(30, 90, 200));

    for (const QPoint origin : {QPoint(80, 40), QPoint(40, 20), QPoint(119, 59)}) {
        verifySpan(scanned(image, origin, Qt::Horizontal), 40, 119, true, true);
        verifySpan(scanned(image, origin, Qt::Vertical), 20, 59, true, true);
    }
    QCOMPARE(scanned(image, QPoint(80, 40), Qt::Horizontal).length(), 80);
    QCOMPARE(scanned(image, QPoint(80, 40), Qt::Vertical).length(), 40);
}

// Spacing between two elements; the column between them has no edge.
void TestMeasureEdgeScanner::gapBetweenElements()
{
    QImage image = filled(200, 100, Qt::white);
    fillRect(image, QRect(QPoint(20, 20), QPoint(59, 59)), Qt::black);
    fillRect(image, QRect(QPoint(100, 20), QPoint(139, 59)), Qt::black);

    const Span gap = scanned(image, QPoint(80, 40), Qt::Horizontal);
    verifySpan(gap, 60, 99, true, true);
    QCOMPARE(gap.length(), 40);
    QVERIFY(!scanned(image, QPoint(80, 40), Qt::Vertical).isReliable());
}

// Flat UI keeps differences of a few levels (a card on a panel).
void TestMeasureEdgeScanner::lowContrastEdge()
{
    QImage image = filled(160, 80, gray(240));
    fillRect(image, QRect(QPoint(30, 10), QPoint(129, 69)), gray(236));

    verifySpan(scanned(image, QPoint(80, 40), Qt::Horizontal), 30, 129, true, true);
    verifySpan(scanned(image, QPoint(80, 40), Qt::Vertical), 10, 69, true, true);
}

// A gradient drifts; it never reads as an edge, so nothing reliable is found,
// even when each step (4 levels) is above the flat-UI threshold.
void TestMeasureEdgeScanner::gradientAloneHasNoEdge_data()
{
    QTest::addColumn<int>("levelsPerPixel");
    QTest::newRow("gentle") << 1;
    QTest::newRow("steep") << 4;
}

void TestMeasureEdgeScanner::gradientAloneHasNoEdge()
{
    QFETCH(int, levelsPerPixel);
    const int width = 256 / levelsPerPixel;
    QImage image(width, 64, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, gray(x * levelsPerPixel));
        }
    }
    const Span horizontal = scanned(image, QPoint(width / 2, 32), Qt::Horizontal);
    verifySpan(horizontal, 0, width - 1, false, false);
    QVERIFY(!horizontal.isReliable());
    QVERIFY(!scanned(image, QPoint(width / 2, 32), Qt::Vertical).isReliable());
}

void TestMeasureEdgeScanner::elementOnGradient()
{
    QImage image(300, 100, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(60 + x / 2, 80 + x / 3, 120));
        }
    }
    fillRect(image, QRect(QPoint(120, 30), QPoint(199, 69)), Qt::white);

    verifySpan(scanned(image, QPoint(160, 50), Qt::Horizontal), 120, 199, true, true);
    // From the gradient: the element ends the span, the image border the other end.
    verifySpan(scanned(image, QPoint(40, 50), Qt::Horizontal), 0, 119, false, true);
}

// A card with a soft shadow: the card's edge counts, the shadow ramp does not,
// from inside the card and from the background beyond the shadow alike.
void TestMeasureEdgeScanner::softShadowIsNotAnEdge()
{
    constexpr int kBackground = 200;
    constexpr int kShadowStep = 5;
    QImage image = filled(240, 80, gray(kBackground));
    // Shadow darkens towards the card over x = 84..99.
    for (int x = 84; x <= 99; ++x) {
        fillRect(image, QRect(x, 0, 1, 80), gray(kBackground - (x - 83) * kShadowStep));
    }
    fillRect(image, QRect(QPoint(100, 0), QPoint(179, 79)), Qt::white);

    verifySpan(scanned(image, QPoint(140, 40), Qt::Horizontal), 100, 179, true, true);
    verifySpan(scanned(image, QPoint(40, 40), Qt::Horizontal), 0, 99, false, true);
}

// Left edge pixel 25% covered by the element, right edge pixel 75%.
void TestMeasureEdgeScanner::antiAliasedEdgeGoesToTheSideCoveringMost()
{
    QImage image = filled(160, 40, Qt::white);
    fillRect(image, QRect(QPoint(40, 0), QPoint(119, 39)), Qt::black);
    fillRect(image, QRect(39, 0, 1, 40), gray(191));
    fillRect(image, QRect(120, 0, 1, 40), gray(64));

    verifySpan(scanned(image, QPoint(80, 20), Qt::Horizontal), 40, 120, true, true);
    // From outside, the same split: the gaps meet the element where it does.
    verifySpan(scanned(image, QPoint(10, 20), Qt::Horizontal), 0, 39, false, true);
    verifySpan(scanned(image, QPoint(150, 20), Qt::Horizontal), 121, 159, true, false);
}

void TestMeasureEdgeScanner::noisyBackground()
{
    // Wallpaper-like noise well above the flat-UI threshold.
    constexpr int kNoise = 8;
    QRandomGenerator random(348);
    QImage image(220, 120, QImage::Format_ARGB32);
    const QRect element(QPoint(60, 30), QPoint(139, 89));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const int base = element.contains(x, y) ? 200 : 128;
            image.setPixelColor(x, y, gray(base + random.bounded(-kNoise, kNoise + 1)));
        }
    }
    QVERIFY(MeasureEdgeScanner::noiseLevel(image, QPoint(100, 60), Qt::Horizontal) > 2.0);

    verifySpan(scanned(image, QPoint(100, 60), Qt::Horizontal), 60, 139, true, true);
    verifySpan(scanned(image, QPoint(100, 60), Qt::Vertical), 30, 89, true, true);
    // Noise alone, away from the element, finds nothing.
    QVERIFY(!scanned(image, QPoint(200, 10), Qt::Vertical).isReliable());
}

void TestMeasureEdgeScanner::hairline()
{
    QImage image = filled(100, 60, Qt::white);
    fillRect(image, QRect(50, 0, 1, 60), Qt::black);

    const Span onLine = scanned(image, QPoint(50, 30), Qt::Horizontal);
    verifySpan(onLine, 50, 50, true, true);
    QCOMPARE(onLine.length(), 1);
    verifySpan(scanned(image, QPoint(20, 30), Qt::Horizontal), 0, 49, false, true);
    verifySpan(scanned(image, QPoint(80, 30), Qt::Horizontal), 51, 99, true, false);
    // The image border is where the capture was cut: a span reaching it is
    // not a measurement.
    QVERIFY(!scanned(image, QPoint(20, 30), Qt::Horizontal).isReliable());
}

void TestMeasureEdgeScanner::blankImageIsNotReliable()
{
    const QImage image = filled(120, 80, gray(245));
    const Span horizontal = scanned(image, QPoint(60, 40), Qt::Horizontal);
    const Span vertical = scanned(image, QPoint(60, 40), Qt::Vertical);
    verifySpan(horizontal, 0, 119, false, false);
    verifySpan(vertical, 0, 79, false, false);
    QVERIFY(!horizontal.isReliable());
    QVERIFY(!vertical.isReliable());
}

void TestMeasureEdgeScanner::nearbyEdgesUsePixelBoundaries()
{
    QImage image = filled(100, 80, Qt::white);
    fillRect(image, QRect(40, 20, 10, 40), Qt::black);
    const auto edgeAt = [&](qreal x, qreal radius) {
        return MeasureEdgeScanner::nearbyEdge(image, QPointF(x, 40), Qt::Horizontal, radius);
    };

    QCOMPARE(edgeAt(38.5, 2), std::optional<int>(40));
    QCOMPARE(edgeAt(41.5, 2), std::optional<int>(40));
    QCOMPARE(edgeAt(49.5, 1), std::optional<int>(50));
    QCOMPARE(edgeAt(50.5, 1), std::optional<int>(50));
    QVERIFY(!edgeAt(38.5, 1));
    QVERIFY(!edgeAt(45, 6));  // Equally close to both edges.
    QVERIFY(!edgeAt(0.5, 2)); // The image border supplies no edge.
    fillRect(image, QRect(2, 0, 8, image.height()), Qt::black);
    QCOMPARE(edgeAt(-1, 3), std::optional<int>(2));
    QVERIFY(!edgeAt(-1, 2));
}

void TestMeasureEdgeScanner::nearbyEdgeRejectsEasedGradient_data()
{
    QTest::addColumn<int>("firstColumn");
    QTest::addColumn<QList<int>>("levels");
    QTest::addColumn<qreal>("startX");
    QTest::newRow("ease-in") << 10 << QList<int>{0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 5, 6, 8, 10, 13, 16}
                             << qreal(20);
    QTest::newRow("ease-in-out") << 7 << QList<int>{1, 2, 5, 8, 9, 10} << qreal(4.1);
    QTest::newRow("starts-within-ramp") << 5 << QList<int>{0, 0, 0, 1, 1, 3, 5, 7, 11} << qreal(12.1);
}

void TestMeasureEdgeScanner::nearbyEdgeRejectsEasedGradient()
{
    QFETCH(int, firstColumn);
    QFETCH(QList<int>, levels);
    QFETCH(qreal, startX);
    QImage image = filled(36, 40, Qt::black);
    for (int x = firstColumn; x < image.width(); ++x) {
        const int i = qMin(x - firstColumn, static_cast<int>(levels.size()) - 1);
        fillRect(image, QRect(x, 0, 1, image.height()), gray(levels.at(i)));
    }
    QVERIFY(!MeasureEdgeScanner::nearbyEdge(image, QPointF(startX, 20.5), Qt::Horizontal, 6));
}

// A cut-out insert leaves transparent pixels: alpha alone makes an edge.
void TestMeasureEdgeScanner::transparentStripIsAnEdge()
{
    QImage image = filled(100, 40, Qt::black);
    fillRect(image, QRect(30, 0, 20, 40), QColor(0, 0, 0, 0));
    verifySpan(scanned(image, QPoint(40, 20), Qt::Horizontal), 30, 49, true, true);
}

void TestMeasureEdgeScanner::originOutsideIsClampedAndOtherFormatsRefused()
{
    QImage image = filled(100, 40, Qt::white);
    fillRect(image, QRect(QPoint(0, 0), QPoint(29, 39)), Qt::black);
    verifySpan(scanned(image, QPoint(-10, 20), Qt::Horizontal), 0, 29, false, true);

    QVERIFY(!MeasureEdgeScanner::scan(QImage(), QPoint(), Qt::Horizontal));
    const QImage grayscale = image.convertToFormat(QImage::Format_Grayscale8);
    QVERIFY(!MeasureEdgeScanner::scan(grayscale, QPoint(10, 10), Qt::Horizontal));
}

// A hover reads one row and one column: on an 8K image it stays far below a
// frame, with no per-move copy of the image.
void TestMeasureEdgeScanner::largeImageHoverIsFast()
{
    constexpr int kWidth = 7680;
    constexpr int kHeight = 4320;
    constexpr int kHovers = 200;
    constexpr qint64 kMaxAverageMicroseconds = 4000;   // generous for Debug and CI

    QImage image = filled(kWidth, kHeight, Qt::white);
    fillRect(image, QRect(QPoint(3000, 1500), QPoint(3999, 2499)), Qt::black);

    QElapsedTimer timer;
    timer.start();
    int checksum = 0;
    for (int i = 0; i < kHovers; ++i) {
        const QPoint origin(100 + i * 37, 100 + i * 21);
        checksum += scanned(image, origin, Qt::Horizontal).length();
        checksum += scanned(image, origin, Qt::Vertical).length();
    }
    const qint64 averageMicroseconds = timer.nsecsElapsed() / 1000 / kHovers;
    qInfo("8K hover: %lld us per hover (both axes), image %lld MB",
          averageMicroseconds, static_cast<long long>(image.sizeInBytes() / (1024 * 1024)));
    QVERIFY(checksum > 0);
    QVERIFY2(averageMicroseconds < kMaxAverageMicroseconds,
             qPrintable(QStringLiteral("%1 us per hover").arg(averageMicroseconds)));
}

QTEST_MAIN(TestMeasureEdgeScanner)
#include "tst_MeasureEdgeScanner.moc"
