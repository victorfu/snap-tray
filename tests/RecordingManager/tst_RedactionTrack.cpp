#include <QtTest>

#include "recording/RedactionTrack.h"

using namespace SnapTray;

namespace {

// "Sensitive text": a 1-pixel checkerboard, which any real masking destroys.
QImage textLike(QSize size = QSize(200, 100))
{
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixel(x, y, (x + y) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    return image;
}

// How much of the checkerboard detail survives in `rect` (1 = untouched).
double detail(const QImage& image, const QRect& rect)
{
    double sum = 0;
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x < rect.right(); ++x) {
            sum += qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x + 1, y))) / 255.0;
            ++count;
        }
    return count ? sum / count : 0;
}

RedactionTrack trackWith(RedactionMask mask)
{
    RedactionTrack track;
    track.masks.push_back(mask);
    return track;
}

} // namespace

class TestRedactionTrack : public QObject
{
    Q_OBJECT

private slots:
    void solidCoversExactlyTheRect();
    void weakeningStylesRemoveDetail_data();
    void weakeningStylesRemoveDetail();
    void timingIsConservative();
    void throughCropAndScaleRoundsOutward();
    void outsideTheFrameIsIgnored();
    void roundTrip();
};

void TestRedactionTrack::solidCoversExactlyTheRect()
{
    QImage frame = textLike();
    const QRect rect(50, 20, 31, 17); // odd sizes
    trackWith({rect, 0, -1, RedactionMask::Style::Solid}).apply(frame, 1000, QRectF(0, 0, 200, 100));
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x) QCOMPARE(frame.pixel(x, y), qRgb(0, 0, 0));
    // Neighbours keep the picture.
    QCOMPARE(detail(frame, QRect(10, 10, 30, 30)), 1.0);
    QCOMPARE(frame.pixel(rect.right() + 1, rect.top()), textLike().pixel(rect.right() + 1, rect.top()));
}

void TestRedactionTrack::weakeningStylesRemoveDetail_data()
{
    QTest::addColumn<int>("style");
    QTest::newRow("pixelate") << int(RedactionMask::Style::Pixelate);
    QTest::newRow("blur") << int(RedactionMask::Style::Blur);
}

void TestRedactionTrack::weakeningStylesRemoveDetail()
{
    QFETCH(int, style);
    QImage frame = textLike();
    const QRect rect(40, 20, 96, 64);
    trackWith({rect, 0, -1, RedactionMask::Style(style)}).apply(frame, 0, QRectF(0, 0, 200, 100));
    // Fine detail (text strokes) is gone inside, untouched outside.
    QVERIFY2(detail(frame, rect.adjusted(2, 2, -2, -2)) < 0.1,
             qPrintable(QString::number(detail(frame, rect))));
    QCOMPARE(detail(frame, QRect(150, 10, 40, 40)), 1.0);
}

void TestRedactionTrack::timingIsConservative()
{
    const RedactionMask mask{QRect(0, 0, 10, 10), 1000, 2000, RedactionMask::Style::Solid};
    // A frame shown shortly before the mask starts stays on screen into it: masked.
    QVERIFY(mask.isActiveAt(1000 - RedactionTrack::kFrameHoldMs + 1));
    QVERIFY(!mask.isActiveAt(1000 - RedactionTrack::kFrameHoldMs));
    QVERIFY(mask.isActiveAt(1500));
    QVERIFY(mask.isActiveAt(2000)); // the end is inclusive
    QVERIFY(!mask.isActiveAt(2001));
    // Open-ended: to the end of the recording.
    const RedactionMask always{QRect(0, 0, 10, 10), 0, -1, RedactionMask::Style::Solid};
    QVERIFY(always.isActiveAt(0));
    QVERIFY(always.isActiveAt(99999999));
    // Masking only when active.
    QImage frame = textLike();
    trackWith(mask).apply(frame, 3000, QRectF(0, 0, 200, 100));
    QCOMPARE(frame, textLike());
}

void TestRedactionTrack::throughCropAndScaleRoundsOutward()
{
    // The frame shows crop (25,10 100x50) at 2x (200x100); the mask (30,15 11x7)
    // lands at (10,10)-(31.99,23.99): every touched pixel is covered.
    QImage frame = textLike();
    trackWith({QRect(30, 15, 11, 7), 0, -1, RedactionMask::Style::Solid})
        .apply(frame, 0, QRectF(25, 10, 100, 50));
    for (int y = 10; y < 24; ++y)
        for (int x = 10; x < 32; ++x) QCOMPARE(frame.pixel(x, y), qRgb(0, 0, 0));
    QVERIFY(frame.pixel(9, 10) != qRgb(0, 0, 0) || frame.pixel(9, 11) != qRgb(0, 0, 0));
    // A fractional mapping (a zoom-like 1.5x) rounds out, never in.
    QImage scaled = textLike(QSize(150, 75));
    trackWith({QRect(11, 11, 3, 3), 0, -1, RedactionMask::Style::Solid}).apply(scaled, 0, QRectF(0, 0, 100, 50));
    for (int y = 16; y <= 20; ++y)
        for (int x = 16; x <= 20; ++x) QCOMPARE(scaled.pixel(x, y), qRgb(0, 0, 0)); // 16.5..21 -> 16..20
}

void TestRedactionTrack::outsideTheFrameIsIgnored()
{
    QImage frame = textLike();
    // Cropped away entirely: nothing to cover, nothing broken.
    trackWith({QRect(500, 500, 20, 20), 0, -1, RedactionMask::Style::Solid}).apply(frame, 0, QRectF(0, 0, 200, 100));
    QCOMPARE(frame, textLike());
    // Partly inside: the inside part is covered.
    trackWith({QRect(190, 90, 30, 30), 0, -1, RedactionMask::Style::Solid}).apply(frame, 0, QRectF(0, 0, 200, 100));
    QCOMPARE(frame.pixel(199, 99), qRgb(0, 0, 0));
}

void TestRedactionTrack::roundTrip()
{
    RedactionTrack track;
    track.masks = {{QRect(1, 2, 30, 40), 100, -1, RedactionMask::Style::Blur},
                   {QRect(5, 6, 7, 8), 0, 900, RedactionMask::Style::Pixelate}};
    QCOMPARE(RedactionTrack::fromJson(track.toJson()), track);
    // Degenerate masks from a damaged recipe are dropped, not applied as nothing.
    QJsonObject bad = track.toJson();
    QJsonArray masks = bad.value(QStringLiteral("masks")).toArray();
    masks.append(QJsonObject{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0}, {QStringLiteral("w"), 1}, {QStringLiteral("h"), 1}});
    bad.insert(QStringLiteral("masks"), masks);
    QCOMPARE(RedactionTrack::fromJson(bad).masks.size(), size_t(2));
}

QTEST_MAIN(TestRedactionTrack)
#include "tst_RedactionTrack.moc"
