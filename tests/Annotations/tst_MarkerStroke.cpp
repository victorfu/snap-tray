#include <QtTest/QtTest>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QtMath>
#include <algorithm>
#include "annotations/MarkerStroke.h"

/**
 * @brief Tests for MarkerStroke annotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Bounding rectangle calculations
 * - Point addition and path building
 * - Clone functionality
 * - Intersection detection for eraser
 * - Semi-transparent rendering
 */
class TestMarkerStroke : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_EmptyPoints();
    void testConstruction_SinglePoint();
    void testConstruction_MultiplePoints();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();

    // Bounding rect tests
    void testBoundingRect_EmptyPoints();
    void testBoundingRect_SinglePoint();
    void testBoundingRect_HorizontalLine();
    void testBoundingRect_VerticalLine();
    void testBoundingRect_DiagonalLine();
    void testBoundingRect_IncludesWidth();
    void testBoundingRect_FractionalPointsUsesCoveringBounds();

    // Point addition tests
    void testAddPoint_IncrementsBoundingRect();
    void testAddPoint_BoundingRectCoversInitialAndFractionalAppendedPoints();
    void testAddPoint_BuildsPath();
    void testAddPoint_MultiplePoints();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPoints();
    void testClone_PreservesColor();
    void testClone_PreservesWidth();

    // Intersection tests (for eraser)
    void testIntersectsCircle_NoIntersection();
    void testIntersectsCircle_Intersection();
    void testIntersectsCircle_EdgeCase();
    void testIntersectsCircle_EmptyStroke();

    // Stroke path tests
    void testStrokePath_EmptyPoints();
    void testStrokePath_SinglePoint();
    void testStrokePath_ValidPath();

    // Drawing tests
    void testDraw_MultiplePoints();
    void testDrawPreview_MultiplePoints();
    void testDrawPreview_MatchesCommittedAcrossDpr();
    void testDraw_JitterPath_MatchesMidpointReference();
    void testTranslate_UpdatesBoundingRectAfterCacheWarmup();
    void testTranslate_RebuildsRenderedCacheAtNewPosition();

private:
    QVector<QPointF> createTestPoints(int count, qreal spacing = 10.0);
};

QVector<QPointF> TestMarkerStroke::createTestPoints(int count, qreal spacing)
{
    QVector<QPointF> points;
    for (int i = 0; i < count; ++i) {
        points.append(QPointF(100 + i * spacing, 100 + i * spacing));
    }
    return points;
}

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

QPainterPath buildMidpointReferencePath(const QVector<QPointF>& points)
{
    QPainterPath path;

    if (points.size() < 2) {
        return path;
    }

    if (points.size() == 2) {
        path.moveTo(points[0]);
        path.lineTo(points[1]);
        return path;
    }

    path.moveTo(points[0]);

    QPointF mid0 = (points[0] + points[1]) / 2.0;
    path.lineTo(mid0);

    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF mid = (points[i] + points[i + 1]) / 2.0;
        path.quadTo(points[i], mid);
    }

    path.quadTo(points[points.size() - 1], points.last());
    return path;
}

QImage renderMidpointReferenceMarker(const QVector<QPointF>& points,
                                     const QColor& color,
                                     int width,
                                     const QSize& imageSize)
{
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.setOpacity(0.4);
    painter.drawPath(buildMidpointReferencePath(points));

    return image;
}

QImage renderMarkerStroke(const MarkerStroke& stroke, const QSize& logicalSize,
                          qreal dpr, bool preview)
{
    const QSize physicalSize(qCeil(logicalSize.width() * dpr),
                             qCeil(logicalSize.height() * dpr));
    QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::white);

    QPainter painter(&image);
    if (preview) {
        stroke.drawPreview(painter);
    } else {
        stroke.draw(painter);
    }
    painter.end();

    return image;
}

bool imagesMatchWithinTolerance(const QImage& actual,
                                const QImage& expected,
                                const QRect& logicalBounds,
                                qreal dpr)
{
    if (actual.size() != expected.size()) {
        return false;
    }

    const QRect physicalBounds = QRect(
        qFloor(logicalBounds.left() * dpr),
        qFloor(logicalBounds.top() * dpr),
        qCeil(logicalBounds.width() * dpr),
        qCeil(logicalBounds.height() * dpr)).intersected(actual.rect());

    int comparedPixels = 0;
    int differingPixels = 0;
    for (int y = physicalBounds.top(); y <= physicalBounds.bottom(); ++y) {
        for (int x = physicalBounds.left(); x <= physicalBounds.right(); ++x) {
            ++comparedPixels;
            const QColor a = actual.pixelColor(x, y);
            const QColor e = expected.pixelColor(x, y);
            const int maxDelta = std::max({
                qAbs(a.red() - e.red()),
                qAbs(a.green() - e.green()),
                qAbs(a.blue() - e.blue()),
                qAbs(a.alpha() - e.alpha())
            });
            if (maxDelta > 8) {
                ++differingPixels;
            }
        }
    }

    return comparedPixels > 0 && differingPixels * 200 <= comparedPixels;
}

// ============================================================================
// Construction Tests
// ============================================================================

void TestMarkerStroke::testConstruction_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);
    QVERIFY(!stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testConstruction_WithColor()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::green, 20);

    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
}

void TestMarkerStroke::testConstruction_WithWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke strokeThin(points, Qt::yellow, 10);
    MarkerStroke strokeThick(points, Qt::yellow, 40);

    // Thicker stroke should have larger bounding rect
    QVERIFY(strokeThick.boundingRect().width() > strokeThin.boundingRect().width());
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestMarkerStroke::testBoundingRect_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());
}

void TestMarkerStroke::testBoundingRect_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestMarkerStroke::testBoundingRect_HorizontalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 100)));
    QVERIFY(rect.width() >= 100);
}

void TestMarkerStroke::testBoundingRect_VerticalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(100, 200) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(100, 200)));
    QVERIFY(rect.height() >= 100);
}

void TestMarkerStroke::testBoundingRect_DiagonalLine()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 200) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
    QVERIFY(rect.contains(QPoint(200, 200)));
}

void TestMarkerStroke::testBoundingRect_IncludesWidth()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };

    MarkerStroke thinStroke(points, Qt::yellow, 10);
    MarkerStroke thickStroke(points, Qt::yellow, 40);

    // Thick stroke bounding rect should be larger due to pen width
    QVERIFY(thickStroke.boundingRect().height() > thinStroke.boundingRect().height());
}

void TestMarkerStroke::testBoundingRect_FractionalPointsUsesCoveringBounds()
{
    const QVector<QPointF> points = {
        QPointF(10.25, 20.75),
        QPointF(50.8, 24.2),
        QPointF(75.6, 55.9)
    };
    MarkerStroke stroke(points, Qt::yellow, 20);

    const QRect rect = stroke.boundingRect();
    const int margin = 20 / 2 + 1;
    const QRect pointBounds = QRectF(
        QPointF(10.25, 20.75),
        QPointF(75.6, 55.9)).normalized().toAlignedRect();

    QCOMPARE(rect, pointBounds.adjusted(-margin, -margin, margin, margin));
    QVERIFY(rect.contains(QPoint(76, 56)));
}

// ============================================================================
// Point Addition Tests
// ============================================================================

void TestMarkerStroke::testAddPoint_IncrementsBoundingRect()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(stroke.boundingRect().isEmpty());

    stroke.addPoint(QPointF(100, 100));
    QRect rect1 = stroke.boundingRect();
    QVERIFY(!rect1.isEmpty());

    stroke.addPoint(QPointF(200, 200));
    QRect rect2 = stroke.boundingRect();
    QVERIFY(rect2.width() > rect1.width() || rect2.height() > rect1.height());
}

void TestMarkerStroke::testAddPoint_BoundingRectCoversInitialAndFractionalAppendedPoints()
{
    MarkerStroke stroke({QPointF(10.25, 20.75)}, Qt::yellow, 20);

    stroke.addPoint(QPointF(80.6, 65.4));

    const QRect rect = stroke.boundingRect();
    QVERIFY(rect.contains(QPoint(10, 20)));
    QVERIFY(rect.contains(QPoint(81, 66)));
}

void TestMarkerStroke::testAddPoint_BuildsPath()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);

    for (int i = 0; i < 10; ++i) {
        stroke.addPoint(QPointF(100 + i * 10, 100 + i * 5));
    }

    QPainterPath path = stroke.strokePath();
    QVERIFY(!path.isEmpty());
}

void TestMarkerStroke::testAddPoint_MultiplePoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);

    for (int i = 0; i < 20; ++i) {
        stroke.addPoint(QPointF(100 + i * 5, 100 + qSin(i) * 20));
    }

    QRect rect = stroke.boundingRect();
    QVERIFY(rect.width() >= 95);  // At least 19 * 5 = 95
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestMarkerStroke::testClone_CreatesNewInstance()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    auto cloned = stroke.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &stroke);
}

void TestMarkerStroke::testClone_PreservesPoints()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

void TestMarkerStroke::testClone_PreservesColor()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::green, 20);

    auto cloned = stroke.clone();

    // Draw both and compare
    QImage img1(200, 200, QImage::Format_ARGB32);
    QImage img2(200, 200, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    stroke.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestMarkerStroke::testClone_PreservesWidth()
{
    QVector<QPointF> points = createTestPoints(3);
    MarkerStroke stroke(points, Qt::yellow, 30);

    auto cloned = stroke.clone();
    QCOMPARE(cloned->boundingRect(), stroke.boundingRect());
}

// ============================================================================
// Intersection Tests
// ============================================================================

void TestMarkerStroke::testIntersectsCircle_NoIntersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle far from the stroke
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 300), 10));
}

void TestMarkerStroke::testIntersectsCircle_Intersection()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle on the stroke
    QVERIFY(stroke.intersectsCircle(QPoint(150, 100), 10));
}

void TestMarkerStroke::testIntersectsCircle_EdgeCase()
{
    QVector<QPointF> points = { QPointF(100, 100), QPointF(200, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Circle just touching the edge
    QVERIFY(stroke.intersectsCircle(QPoint(100, 100), 15));
}

void TestMarkerStroke::testIntersectsCircle_EmptyStroke()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QVERIFY(!stroke.intersectsCircle(QPoint(100, 100), 10));
}

// ============================================================================
// Stroke Path Tests
// ============================================================================

void TestMarkerStroke::testStrokePath_EmptyPoints()
{
    MarkerStroke stroke(QVector<QPointF>(), Qt::yellow, 20);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());
}

void TestMarkerStroke::testStrokePath_SinglePoint()
{
    QVector<QPointF> points = { QPointF(100, 100) };
    MarkerStroke stroke(points, Qt::yellow, 20);
    QPainterPath path = stroke.strokePath();
    QVERIFY(path.isEmpty());  // Need at least 2 points for a path
}

void TestMarkerStroke::testStrokePath_ValidPath()
{
    QVector<QPointF> points = createTestPoints(5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    QPainterPath path = stroke.strokePath();
    QVERIFY(!path.isEmpty());

    // Path should contain all the points
    for (const QPointF& pt : points) {
        QVERIFY(path.contains(pt));
    }
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestMarkerStroke::testDraw_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(10, 5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    stroke.draw(painter);
    painter.end();

    // Verify something was drawn (not all white)
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestMarkerStroke::testDrawPreview_MultiplePoints()
{
    QVector<QPointF> points = createTestPoints(10, 5);
    MarkerStroke stroke(points, Qt::yellow, 20);

    QImage image(200, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    stroke.drawPreview(painter);
    painter.end();

    QVERIFY(regionHasNonWhitePixel(image, stroke.boundingRect().adjusted(-2, -2, 2, 2)));
}

void TestMarkerStroke::testDrawPreview_MatchesCommittedAcrossDpr()
{
    const QVector<QPointF> points = {
        QPointF(35.25, 82.75),
        QPointF(58.5, 45.25),
        QPointF(83.75, 112.5),
        QPointF(125.4, 68.8),
        QPointF(164.2, 122.6),
        QPointF(206.6, 76.4)
    };
    MarkerStroke stroke(points, Qt::yellow, 20);
    const QSize logicalSize(260, 180);

    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0}) {
        const QImage committed = renderMarkerStroke(stroke, logicalSize, dpr, false);
        const QImage preview = renderMarkerStroke(stroke, logicalSize, dpr, true);
        QVERIFY2(imagesMatchWithinTolerance(
                     preview,
                     committed,
                     stroke.boundingRect().adjusted(-2, -2, 2, 2),
                     dpr),
                 qPrintable(QStringLiteral("Preview and committed marker diverged at DPR %1").arg(dpr)));
    }
}

void TestMarkerStroke::testDraw_JitterPath_MatchesMidpointReference()
{
    const QVector<QPointF> points = {
        QPointF(40, 110),
        QPointF(70, 62),
        QPointF(95, 146),
        QPointF(124, 84),
        QPointF(156, 151),
        QPointF(185, 92),
        QPointF(212, 136)
    };

    MarkerStroke stroke(points, Qt::yellow, 20);

    QImage actual(280, 220, QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::white);
    {
        QPainter painter(&actual);
        stroke.draw(painter);
    }

    const QImage expected = renderMidpointReferenceMarker(points, Qt::yellow, 20, actual.size());
    QCOMPARE(actual, expected);
}

void TestMarkerStroke::testTranslate_UpdatesBoundingRectAfterCacheWarmup()
{
    QVector<QPointF> points = {
        QPointF(40, 40),
        QPointF(90, 65),
        QPointF(140, 55)
    };
    MarkerStroke stroke(points, Qt::yellow, 20);

    // Warm render cache before translation.
    QImage warmup(260, 180, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    stroke.draw(warmPainter);

    const QRect originalRect = stroke.boundingRect();
    const QPointF delta(48.0, 26.0);
    stroke.translate(delta);

    const QRect translatedRect = stroke.boundingRect();
    QCOMPARE(translatedRect.topLeft(), originalRect.topLeft() + QPoint(48, 26));
    QCOMPARE(translatedRect.size(), originalRect.size());
}

void TestMarkerStroke::testTranslate_RebuildsRenderedCacheAtNewPosition()
{
    QVector<QPointF> points = {
        QPointF(30, 30),
        QPointF(60, 50),
        QPointF(90, 35),
        QPointF(120, 60)
    };
    MarkerStroke stroke(points, Qt::yellow, 18);

    // Build initial cache.
    QImage warmup(300, 220, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    stroke.draw(warmPainter);

    const QRect oldRect = stroke.boundingRect();
    stroke.translate(QPointF(120.0, 70.0));
    const QRect newRect = stroke.boundingRect();

    QImage translatedImage(300, 220, QImage::Format_ARGB32);
    translatedImage.fill(Qt::white);
    QPainter translatedPainter(&translatedImage);
    stroke.draw(translatedPainter);

    QVERIFY(regionHasNonWhitePixel(translatedImage, newRect.adjusted(-2, -2, 2, 2)));
    QVERIFY(!regionHasNonWhitePixel(translatedImage, oldRect.adjusted(-2, -2, 2, 2)));
}

QTEST_MAIN(TestMarkerStroke)
#include "tst_MarkerStroke.moc"
