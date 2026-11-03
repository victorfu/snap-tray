#include <QtTest>

#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "recording/OverlayTrack.h"

using namespace SnapTray;

namespace {

QRect changed(const QImage& a, const QImage& b)
{
    QRect box;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) box |= QRect(x, y, 1, 1);
    return box;
}

QImage blank(const QSize& size = QSize(400, 200))
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(30, 90, 160));
    return image;
}

OverlaySegment title(const QString& text)
{
    OverlaySegment s;
    s.kind = OverlaySegment::Kind::Text;
    s.text = text;
    s.startMs = 1000;
    s.endMs = 3000;
    s.position = QPointF(0.1, 0.2);
    s.size = 0.1;
    return s;
}

} // namespace

class TestOverlayTrack : public QObject
{
    Q_OBJECT

private slots:
    void boundsFollowTheContentAndThePicture();
    void fadesInAndOut();
    void drawsOnlyInsideItsBoundsAndTime();
    void imageKeepsItsShape();
    void cjkTextRenders();
    void jsonRoundTrip();
    void largeImagesAreBounded();
    void previewDrawsOnlyOnThePicture();
};

void TestOverlayTrack::boundsFollowTheContentAndThePicture()
{
    const QSize picture(400, 200);
    const QRectF shortText = OverlayTrack::boundsOf(title(QStringLiteral("Hi")), picture);
    const QRectF longText = OverlayTrack::boundsOf(title(QStringLiteral("A much longer title")), picture);
    QCOMPARE(shortText.topLeft(), QPointF(40, 40));
    QVERIFY(longText.width() > shortText.width());
    const QRectF twoLines = OverlayTrack::boundsOf(title(QStringLiteral("Hi\nthere")), picture);
    QVERIFY(twoLines.height() > shortText.height() * 1.5);
    // Relative to the picture: twice the picture, twice the box.
    const QRectF bigger = OverlayTrack::boundsOf(title(QStringLiteral("Hi")), picture * 2);
    QVERIFY(qAbs(bigger.width() - 2 * shortText.width()) <= 4);
    QCOMPARE(bigger.topLeft(), QPointF(80, 80));
}

void TestOverlayTrack::fadesInAndOut()
{
    OverlaySegment s = title(QStringLiteral("x"));
    QCOMPARE(s.opacityAt(999), 0.0);
    QCOMPARE(s.opacityAt(1000), 0.0);
    QVERIFY(qAbs(s.opacityAt(1150) - 0.5) < 0.01);
    QCOMPARE(s.opacityAt(2000), 1.0);
    QVERIFY(qAbs(s.opacityAt(2850) - 0.5) < 0.01);
    QCOMPARE(s.opacityAt(3000), 0.0);
    s.endMs = -1; // to the end: no fade out
    QCOMPARE(s.opacityAt(999999), 1.0);
    s.fadeMs = 0;
    QCOMPARE(s.opacityAt(1000), 1.0);
}

void TestOverlayTrack::drawsOnlyInsideItsBoundsAndTime()
{
    OverlayTrack track;
    track.segments.push_back(title(QStringLiteral("Title")));
    const QImage base = blank();
    QImage frame = base;
    track.paint(frame, 2000);
    const QRect drawn = changed(frame, base);
    const QRectF bounds = OverlayTrack::boundsOf(track.segments[0], base.size());
    QVERIFY(!drawn.isEmpty());
    QVERIFY2(bounds.toAlignedRect().adjusted(-1, -1, 1, 1).contains(drawn),
             qPrintable(QStringLiteral("%1,%2 %3x%4").arg(drawn.x()).arg(drawn.y()).arg(drawn.width()).arg(drawn.height())));
    QImage before = base;
    track.paint(before, 500);
    QCOMPARE(before, base);
    // Same time, same pixels; a view rect at half size draws the same layout, scaled.
    QImage again = base;
    track.paint(again, 2000);
    QCOMPARE(again, frame);
    QImage half(base.size() / 2, QImage::Format_RGB32);
    half.fill(QColor(30, 90, 160));
    const QImage halfBase = half;
    QPainter painter(&half);
    track.paint(painter, 2000, QRectF(QPointF(0, 0), QSizeF(half.size())), base.size());
    painter.end();
    const QRect halfDrawn = changed(half, halfBase);
    QVERIFY(qAbs(halfDrawn.left() - drawn.left() / 2) <= 2 && qAbs(halfDrawn.top() - drawn.top() / 2) <= 2);
}

void TestOverlayTrack::imageKeepsItsShape()
{
    OverlaySegment logo;
    logo.kind = OverlaySegment::Kind::Image;
    logo.image = QImage(100, 50, QImage::Format_ARGB32_Premultiplied);
    logo.image.fill(Qt::red);
    logo.size = 0.25; // a quarter of the width
    logo.position = QPointF(0.5, 0.5);
    logo.fadeMs = 0;
    const QRectF bounds = OverlayTrack::boundsOf(logo, QSize(400, 200));
    QCOMPARE(bounds, QRectF(200, 100, 100, 50));
    OverlayTrack track;
    track.segments.push_back(logo);
    QImage frame = blank();
    track.paint(frame, 10);
    QCOMPARE(frame.pixelColor(250, 125), QColor(Qt::red));
    logo.opacity = 0.5;
    track.segments[0] = logo;
    QImage faded = blank();
    track.paint(faded, 10);
    const QColor mixed = faded.pixelColor(250, 125);
    QVERIFY(mixed.red() > 100 && mixed.red() < 220);
}

void TestOverlayTrack::cjkTextRenders()
{
    OverlayTrack track;
    track.segments.push_back(title(QStringLiteral("\u6A19\u984C \u30BF\u30A4\u30C8\u30EB")));
    QImage frame = blank();
    track.paint(frame, 2000);
    QVERIFY(!changed(frame, blank()).isEmpty());
}

// PR #201 review: an overlay reaching past the picture is cut off in the
// export, so the preview (a picture shown inside a larger view) cuts it too.
void TestOverlayTrack::previewDrawsOnlyOnThePicture()
{
    OverlayTrack track;
    OverlaySegment image;
    image.kind = OverlaySegment::Kind::Image;
    image.image = QImage(200, 200, QImage::Format_ARGB32_Premultiplied);
    image.image.fill(Qt::white);
    image.position = QPointF(0.5, 0.5);
    image.size = 1.0; // far past the picture's lower right
    image.startMs = 0;
    image.endMs = -1;
    image.fadeMs = 0;
    track.segments = {image};
    const QSize picture(200, 100);
    QImage view(400, 300, QImage::Format_RGB32);
    view.fill(Qt::black);
    const QRectF target(100, 100, 200, 100); // where the picture is shown
    {
        QPainter painter(&view);
        track.paint(painter, 500, target, picture);
    }
    QCOMPARE(view.pixelColor(290, 190), QColor(Qt::white));  // on the picture
    QCOMPARE(view.pixelColor(310, 190), QColor(Qt::black));  // right of it
    QCOMPARE(view.pixelColor(290, 210), QColor(Qt::black));  // below it
}

void TestOverlayTrack::jsonRoundTrip()
{
    OverlayTrack track;
    OverlaySegment text = title(QStringLiteral("Hello"));
    text.color = QColor(255, 214, 10);
    text.background = OverlaySegment::Background::Light;
    text.align = OverlaySegment::Align::Center;
    OverlaySegment image;
    image.kind = OverlaySegment::Kind::Image;
    image.image = QImage(8, 4, QImage::Format_ARGB32_Premultiplied);
    image.image.fill(Qt::green);
    image.opacity = 0.75;
    track.segments = {text, image};
    const OverlayTrack back = OverlayTrack::fromJson(track.toJson());
    QCOMPARE(int(back.segments.size()), 2);
    QCOMPARE(back.segments[0], text);
    QCOMPARE(back.segments[1].image.size(), QSize(8, 4));
    QCOMPARE(back.segments[1].image.pixelColor(1, 1), QColor(Qt::green));
    QCOMPARE(back.segments[1].opacity, 0.75);
    // Broken entries are dropped, never half-read.
    QJsonObject broken = track.toJson();
    QJsonArray list = broken.value(QStringLiteral("segments")).toArray();
    QJsonObject bad = list[1].toObject();
    bad.insert(QStringLiteral("png"), QStringLiteral("not an image"));
    list[1] = bad;
    broken.insert(QStringLiteral("segments"), list);
    QCOMPARE(int(OverlayTrack::fromJson(broken).segments.size()), 1);
}

void TestOverlayTrack::largeImagesAreBounded()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("big.png"));
    QImage big(4000, 1000, QImage::Format_ARGB32);
    big.fill(Qt::blue);
    QVERIFY(big.save(path));
    const QImage loaded = OverlayTrack::loadImage(path);
    QCOMPARE(loaded.size(), QSize(OverlaySegment::kMaxImageEdge, 512));
    // Kept in memory: removing the file changes nothing for the overlay.
    QFile::remove(path);
    QVERIFY(!loaded.isNull());
    QVERIFY(OverlayTrack::loadImage(dir.filePath(QStringLiteral("missing.png"))).isNull());
}

QTEST_MAIN(TestOverlayTrack)
#include "tst_OverlayTrack.moc"
