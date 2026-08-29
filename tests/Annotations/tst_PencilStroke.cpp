#include <QtTest/QtTest>
#include <QImage>
#include <QPainter>
#include <cmath>

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

QImage renderStroke(PencilStroke& stroke,
                    bool preview,
                    qreal devicePixelRatio,
                    qreal rotationDegrees)
{
    const QSize logicalSize(700, 500);
    QImage image(
        QSize(qRound(logicalSize.width() * devicePixelRatio),
              qRound(logicalSize.height() * devicePixelRatio)),
        QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setClipRect(QRect(QPoint(0, 0), logicalSize));
    if (!qFuzzyIsNull(rotationDegrees)) {
        const QPointF center(
            logicalSize.width() / 2.0,
            logicalSize.height() / 2.0);
        painter.translate(center);
        painter.rotate(rotationDegrees);
        painter.translate(-center);
    }
    if (preview) {
        stroke.drawPreview(painter);
    } else {
        stroke.draw(painter);
    }
    return image;
}

quint64 totalAlpha(const QImage& image)
{
    quint64 total = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            total += qAlpha(image.pixel(x, y));
        }
    }
    return total;
}

QRect alphaBounds(const QImage& image)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 8) {
                continue;
            }
            const QRect pixelRect(x, y, 1, 1);
            bounds = bounds.isValid() ? bounds.united(pixelRect) : pixelRect;
        }
    }
    return bounds;
}

QPair<int, int> pixelDifferenceSummary(const QImage& lhs, const QImage& rhs)
{
    int differingPixels = 0;
    int maxAlphaDifference = 0;
    for (int y = 0; y < lhs.height(); ++y) {
        for (int x = 0; x < lhs.width(); ++x) {
            const QRgb lhsPixel = lhs.pixel(x, y);
            const QRgb rhsPixel = rhs.pixel(x, y);
            if (lhsPixel != rhsPixel) {
                ++differingPixels;
                maxAlphaDifference = qMax(
                    maxAlphaDifference,
                    qAbs(qAlpha(lhsPixel) - qAlpha(rhsPixel)));
            }
        }
    }
    return {differingPixels, maxAlphaDifference};
}

}  // namespace

class TestPencilStroke : public QObject
{
    Q_OBJECT

private slots:
    void testAddPoint_BoundingRectPreservesConstructorStartPoint();
    void testIntersectsCircle_FollowsSmoothPathAtSharpTurn();
    void testIntersectsCircle_BoundingRectCoversSmoothOvershoot();
    void testPreviewAffectedBounds_IncludeNewlyLockedOvershoot();
    void testTranslate_UpdatesBoundingRectAfterCacheWarmup();
    void testTranslate_PreservesCachedPathPlacement();
    void testPreviewRasterMatchesVectorCoverage_data();
    void testPreviewRasterMatchesVectorCoverage();
    void testFinalizePreservesRendering_data();
    void testFinalizePreservesRendering();
    void testPreviewRasterPreservesSelfOverlapAlpha_data();
    void testPreviewRasterPreservesSelfOverlapAlpha();
};

void TestPencilStroke::testAddPoint_BoundingRectPreservesConstructorStartPoint()
{
    PencilStroke stroke({QPointF(20, 20)}, Qt::red, 4, LineStyle::Solid);

    stroke.addPoint(QPointF(200, 200));

    QVERIFY(stroke.boundingRect().contains(QPoint(20, 20)));
    QVERIFY(stroke.intersectsCircle(QPoint(20, 20), 1));
}

void TestPencilStroke::testIntersectsCircle_FollowsSmoothPathAtSharpTurn()
{
    const QVector<QPointF> points = {
        QPointF(20, 200),
        QPointF(200, 20),
        QPointF(380, 200)
    };
    PencilStroke stroke(points, Qt::red, 4, LineStyle::Solid);

    // The centripetal Catmull-Rom curve passes through (110, 87.5), while the
    // raw control-point polyline passes through (110, 110).
    QVERIFY(stroke.intersectsCircle(QPoint(110, 88), 1));
    QVERIFY(!stroke.intersectsCircle(QPoint(110, 110), 1));
}

void TestPencilStroke::testIntersectsCircle_BoundingRectCoversSmoothOvershoot()
{
    const QVector<QPointF> points = {
        QPointF(20, 100),
        QPointF(420, 100),
        QPointF(420, 500),
        QPointF(820, 500)
    };
    PencilStroke stroke(points, Qt::red, 4, LineStyle::Solid);

    // The first Catmull-Rom segment overshoots above every sampled point.
    // Its visible centerline is near (316, 70), so the bounding-box early
    // rejection must include the curve rather than only the raw points.
    QVERIFY(stroke.boundingRect().contains(QPoint(316, 70)));
    QVERIFY(stroke.intersectsCircle(QPoint(316, 70), 10));
}

void TestPencilStroke::testPreviewAffectedBounds_IncludeNewlyLockedOvershoot()
{
    PencilStroke stroke(
        {QPointF(20, 100), QPointF(420, 100), QPointF(420, 500)},
        Qt::red,
        4,
        LineStyle::Solid);

    stroke.addPoint(QPointF(820, 500));

    // Adding the fourth point changes segment 0 before locking it. Its smooth
    // curve overshoots above the raw points, so the repaint bounds must include
    // that newly locked geometry rather than only the still-live tail.
    QVERIFY(stroke.previewAffectedBoundingRect().contains(QPoint(316, 70)));
}

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

void TestPencilStroke::testTranslate_PreservesCachedPathPlacement()
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

void TestPencilStroke::testPreviewRasterMatchesVectorCoverage_data()
{
    QTest::addColumn<int>("lineStyle");
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::addColumn<qreal>("rotationDegrees");

    for (const auto style : {LineStyle::Solid, LineStyle::Dashed, LineStyle::Dotted}) {
        for (const qreal dpr : {1.0, 1.5, 2.0}) {
            const QByteArray rowName = QByteArray::number(static_cast<int>(style)) +
                "-dpr-" + QByteArray::number(dpr);
            QTest::newRow(rowName.constData())
                << static_cast<int>(style) << dpr << 0.0;
        }
        const QByteArray rotatedRowName =
            QByteArray::number(static_cast<int>(style)) + "-rotated";
        QTest::newRow(rotatedRowName.constData())
            << static_cast<int>(style) << 1.0 << 90.0;
    }
}

void TestPencilStroke::testPreviewRasterMatchesVectorCoverage()
{
    QFETCH(int, lineStyle);
    QFETCH(qreal, devicePixelRatio);
    QFETCH(qreal, rotationDegrees);

    PencilStroke stroke(
        {QPointF(20.0, 250.0)},
        QColor(220, 20, 70),
        5,
        static_cast<LineStyle>(lineStyle));
    for (int i = 1; i < 180; ++i) {
        stroke.addPoint(QPointF(
            20.0 + i * 3.4,
            250.0 + 110.0 * std::sin(i * 0.13)));
    }

    const QImage vectorImage = renderStroke(
        stroke, false, devicePixelRatio, rotationDegrees);
    const QImage previewImage = renderStroke(
        stroke, true, devicePixelRatio, rotationDegrees);
    if (static_cast<LineStyle>(lineStyle) != LineStyle::Solid ||
        qFuzzyCompare(devicePixelRatio, 1.5)) {
        QCOMPARE(previewImage, vectorImage);
    } else {
        const auto [differingPixels, maxAlphaDifference] =
            pixelDifferenceSummary(previewImage, vectorImage);
        QVERIFY2(differingPixels <= 128,
                 qPrintable(QStringLiteral("differing pixels: %1").arg(differingPixels)));
        QVERIFY2(maxAlphaDifference <= 160,
                 qPrintable(QStringLiteral("max alpha difference: %1")
                                .arg(maxAlphaDifference)));
    }
    const quint64 vectorAlpha = totalAlpha(vectorImage);
    const quint64 previewAlpha = totalAlpha(previewImage);
    QVERIFY(vectorAlpha > 0);
    const qreal alphaRatio = static_cast<qreal>(previewAlpha) / vectorAlpha;
    QVERIFY2(alphaRatio > 0.97 && alphaRatio < 1.03,
             qPrintable(QStringLiteral("alpha ratio: %1").arg(alphaRatio)));

    const QRect vectorBounds = alphaBounds(vectorImage);
    const QRect previewBounds = alphaBounds(previewImage);
    QVERIFY(vectorBounds.isValid());
    QVERIFY(previewBounds.isValid());
    QVERIFY(vectorBounds.adjusted(-2, -2, 2, 2).contains(previewBounds));
    QVERIFY(previewBounds.adjusted(-2, -2, 2, 2).contains(vectorBounds));
}

void TestPencilStroke::testFinalizePreservesRendering_data()
{
    QTest::addColumn<int>("lineStyle");
    QTest::newRow("solid") << static_cast<int>(LineStyle::Solid);
    QTest::newRow("dashed") << static_cast<int>(LineStyle::Dashed);
    QTest::newRow("dotted") << static_cast<int>(LineStyle::Dotted);
}

void TestPencilStroke::testFinalizePreservesRendering()
{
    QFETCH(int, lineStyle);

    PencilStroke stroke(
        {QPointF(20.0, 250.0)}, Qt::red, 5, static_cast<LineStyle>(lineStyle));
    for (int i = 1; i < 90; ++i) {
        stroke.addPoint(QPointF(
            20.0 + i * 6.0,
            250.0 + 80.0 * std::sin(i * 0.17)));
    }

    const QImage before = renderStroke(stroke, false, 1.0, 0.0);
    stroke.finalize();
    const QImage after = renderStroke(stroke, false, 1.0, 0.0);
    QCOMPARE(after, before);
}

void TestPencilStroke::testPreviewRasterPreservesSelfOverlapAlpha_data()
{
    QTest::addColumn<int>("lineStyle");
    QTest::newRow("solid") << static_cast<int>(LineStyle::Solid);
    QTest::newRow("dashed") << static_cast<int>(LineStyle::Dashed);
    QTest::newRow("dotted") << static_cast<int>(LineStyle::Dotted);
}

void TestPencilStroke::testPreviewRasterPreservesSelfOverlapAlpha()
{
    QFETCH(int, lineStyle);

    PencilStroke stroke(
        {QPointF(330.0, 250.0)},
        QColor(220, 20, 70, 128),
        7,
        static_cast<LineStyle>(lineStyle));
    constexpr qreal kPi = 3.14159265358979323846;
    for (int i = 1; i < 480; ++i) {
        const qreal angle = i * 2.0 * kPi / 160.0;
        stroke.addPoint(QPointF(
            250.0 + 80.0 * std::cos(angle),
            250.0 + 80.0 * std::sin(angle)));
    }

    const QImage vectorImage = renderStroke(stroke, false, 1.0, 0.0);
    const QImage previewImage = renderStroke(stroke, true, 1.0, 0.0);
    QCOMPARE(previewImage, vectorImage);
}

QTEST_MAIN(TestPencilStroke)
#include "tst_PencilStroke.moc"
