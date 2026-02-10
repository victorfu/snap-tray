#include <QtTest>
#include <QPainter>

#include "scrollcapture/ScrollStitcher.h"

namespace {
QImage buildCanvas(int width, int height)
{
    QImage canvas(width, height, QImage::Format_ARGB32);
    canvas.fill(Qt::white);

    QPainter painter(&canvas);
    for (int y = 0; y < height; y += 20) {
        const int shade = (y / 20) % 2 == 0 ? 220 : 245;
        painter.fillRect(0, y, width, 20, QColor(shade, shade, shade));
    }

    painter.setPen(Qt::black);
    for (int y = 40; y < height; y += 120) {
        painter.drawText(24, y, QStringLiteral("Row %1").arg(y));
    }
    painter.end();
    return canvas;
}
} // namespace

class tst_ScrollStitcher : public QObject
{
    Q_OBJECT

private slots:
    void testDownwardMatchAndCompose();
    void testUpwardMatchAndCompose();
};

void tst_ScrollStitcher::testDownwardMatchAndCompose()
{
    constexpr int kFrameWidth = 640;
    constexpr int kFrameHeight = 1000;
    constexpr int kShift = 220;

    const QImage longCanvas = buildCanvas(kFrameWidth, 2800);
    const QImage prev = longCanvas.copy(0, 0, kFrameWidth, kFrameHeight);
    const QImage curr = longCanvas.copy(0, kShift, kFrameWidth, kFrameHeight);

    ScrollStitcher stitcher;
    ScrollStitcher::MatchOptions options;
    options.direction = ScrollDirection::Down;
    options.matchThreshold = 0.55;
    stitcher.setOptions(options);

    QVERIFY(stitcher.initialize(prev));
    const ScrollStitcher::MatchResult result = stitcher.appendFromPair(prev, curr);
    QVERIFY2(result.valid, qPrintable(result.message));
    QVERIFY(result.deltaFullPx > kShift - 30);
    QVERIFY(result.deltaFullPx < kShift + 30);

    const QPixmap composed = stitcher.compose();
    QVERIFY(!composed.isNull());
    QCOMPARE(composed.toImage().height(), kFrameHeight + result.deltaFullPx);
}

void tst_ScrollStitcher::testUpwardMatchAndCompose()
{
    constexpr int kFrameWidth = 640;
    constexpr int kFrameHeight = 900;
    constexpr int kShift = 180;

    const QImage longCanvas = buildCanvas(kFrameWidth, 2600);
    const QImage prev = longCanvas.copy(0, 900, kFrameWidth, kFrameHeight);
    const QImage curr = longCanvas.copy(0, 900 - kShift, kFrameWidth, kFrameHeight);

    ScrollStitcher stitcher;
    ScrollStitcher::MatchOptions options;
    options.direction = ScrollDirection::Up;
    options.matchThreshold = 0.55;
    stitcher.setOptions(options);

    QVERIFY(stitcher.initialize(prev));
    const ScrollStitcher::MatchResult result = stitcher.appendFromPair(prev, curr);
    QVERIFY2(result.valid, qPrintable(result.message));
    QVERIFY(result.deltaFullPx > kShift - 30);
    QVERIFY(result.deltaFullPx < kShift + 30);

    const QPixmap composed = stitcher.compose();
    QVERIFY(!composed.isNull());
    QCOMPARE(composed.toImage().height(), kFrameHeight + result.deltaFullPx);
}

QTEST_MAIN(tst_ScrollStitcher)
#include "tst_ScrollStitcher.moc"
