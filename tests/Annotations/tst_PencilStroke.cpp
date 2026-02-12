#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>

#include "annotations/PencilStroke.h"

namespace {

bool regionHasNonWhitePixel(const QImage& image, const QRect& region)
{
    const QRect clipped = region.intersected(image.rect());
    if (clipped.isEmpty()) {
        return false;
    }

    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() != 255 || color.green() != 255 || color.blue() != 255 || color.alpha() != 255) {
                return true;
            }
        }
    }

    return false;
}

PencilStroke createStrokeWithCachedSegments()
{
    PencilStroke stroke(QVector<QPointF>(), Qt::red, 6, LineStyle::Solid);
    const QVector<QPointF> points = {
        QPointF(20, 20),
        QPointF(40, 30),
        QPointF(60, 44),
        QPointF(80, 62),
        QPointF(100, 78),
        QPointF(120, 90),
        QPointF(140, 104)
    };

    for (const QPointF& point : points) {
        stroke.addPoint(point);
    }

    return stroke;
}

}  // namespace

class TestPencilStroke : public QObject
{
    Q_OBJECT

private slots:
    void testTranslate_UpdatesBoundingRectAfterCacheWarmup();
    void testTranslate_InvalidatesCachedPath();
};

void TestPencilStroke::testTranslate_UpdatesBoundingRectAfterCacheWarmup()
{
    PencilStroke stroke = createStrokeWithCachedSegments();

    QImage warmup(320, 220, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    stroke.draw(warmPainter);

    const QRect originalRect = stroke.boundingRect();
    stroke.translate(QPointF(70.0, 40.0));

    const QRect translatedRect = stroke.boundingRect();
    QCOMPARE(translatedRect.topLeft(), originalRect.topLeft() + QPoint(70, 40));
    QCOMPARE(translatedRect.size(), originalRect.size());
}

void TestPencilStroke::testTranslate_InvalidatesCachedPath()
{
    PencilStroke stroke = createStrokeWithCachedSegments();
    const QRect oldRect = stroke.boundingRect();

    QImage warmup(520, 360, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    stroke.draw(warmPainter);

    stroke.translate(QPointF(180.0, 110.0));
    const QRect newRect = stroke.boundingRect();

    QImage translated(520, 360, QImage::Format_ARGB32);
    translated.fill(Qt::white);
    QPainter translatedPainter(&translated);
    stroke.draw(translatedPainter);

    QVERIFY(regionHasNonWhitePixel(translated, newRect.adjusted(-2, -2, 2, 2)));
    QVERIFY(!regionHasNonWhitePixel(translated, oldRect.adjusted(-2, -2, 2, 2)));
}

QTEST_MAIN(TestPencilStroke)
#include "tst_PencilStroke.moc"
