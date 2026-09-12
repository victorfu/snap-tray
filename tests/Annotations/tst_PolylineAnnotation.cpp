#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/PolylineAnnotation.h"

/**
 * @brief Tests for PolylineAnnotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Point management (add, update, remove, set)
 * - Bounding rectangle calculations
 * - Clone functionality
 * - Hit testing (containsPoint)
 * - Line end styles
 * - Drawing tests
 */
class TestPolylineAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_Empty();
    void testConstruction_WithPoints();
    void testConstruction_WithColor();
    void testConstruction_WithWidth();
    void testConstruction_WithLineEndStyle();
    void testConstruction_WithLineStyle();

    // Point management tests
    void testAddPoint();
    void testUpdateLastPoint();
    void testSetPoint();
    void testRemoveLastPoint();
    void testMoveBy();
    void testPointCount();
    void testPoints();

    // Bounding rect tests
    void testBoundingRect_Empty();
    void testBoundingRect_SinglePoint();
    void testBoundingRect_MultiplePoints();
    void testBoundingRect_AfterAddPoint();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPoints();
    void testClone_PreservesColor();
    void testClone_PreservesStyle();

    // Hit testing tests
    void testContainsPoint_OnSegment();
    void testContainsPoint_OffSegment();
    void testContainsPoint_NearVertex();
    void testContainsPoint_MultipleSegments();

    // Style accessors
    void testLineEndStyle();
    void testColor();
    void testWidth();

    // Drawing tests
    void testDraw_MultiplePoints();
    void testShortTerminalSegments_data();
    void testShortTerminalSegments();
    void testRepeatedAndZeroLengthPoints_data();
    void testRepeatedAndZeroLengthPoints();
    void testHeadAtShortBentEndpoint_data();
    void testHeadAtShortBentEndpoint();

private:
    QVector<QPoint> createTestPoints(int count, int spacing = 20);
};

QVector<QPoint> TestPolylineAnnotation::createTestPoints(int count, int spacing)
{
    QVector<QPoint> points;
    for (int i = 0; i < count; ++i) {
        points.append(QPoint(100 + i * spacing, 100));
    }
    return points;
}

// ============================================================================
// Construction Tests
// ============================================================================

void TestPolylineAnnotation::testConstruction_Empty()
{
    PolylineAnnotation polyline(Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 0);
    QVERIFY(polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testConstruction_WithPoints()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 4);
    QVERIFY(!polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testConstruction_WithColor()
{
    PolylineAnnotation polyline(Qt::blue, 3);
    QCOMPARE(polyline.color(), QColor(Qt::blue));
}

void TestPolylineAnnotation::testConstruction_WithWidth()
{
    PolylineAnnotation polyline(Qt::red, 10);
    QCOMPARE(polyline.width(), 10);
}

void TestPolylineAnnotation::testConstruction_WithLineEndStyle()
{
    PolylineAnnotation polylineNone(Qt::red, 3, LineEndStyle::None);
    PolylineAnnotation polylineEnd(Qt::red, 3, LineEndStyle::EndArrow);
    PolylineAnnotation polylineBoth(Qt::red, 3, LineEndStyle::BothArrow);

    QCOMPARE(polylineNone.lineEndStyle(), LineEndStyle::None);
    QCOMPARE(polylineEnd.lineEndStyle(), LineEndStyle::EndArrow);
    QCOMPARE(polylineBoth.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestPolylineAnnotation::testConstruction_WithLineStyle()
{
    QVector<QPoint> points = createTestPoints(3);

    PolylineAnnotation solidPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Solid);
    PolylineAnnotation dashedPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dashed);
    PolylineAnnotation dottedPolyline(points, Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Dotted);

    // All should have valid bounding rects
    QVERIFY(!solidPolyline.boundingRect().isEmpty());
    QVERIFY(!dashedPolyline.boundingRect().isEmpty());
    QVERIFY(!dottedPolyline.boundingRect().isEmpty());
}

// ============================================================================
// Point Management Tests
// ============================================================================

void TestPolylineAnnotation::testAddPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);

    polyline.addPoint(QPoint(100, 100));
    QCOMPARE(polyline.pointCount(), 1);

    polyline.addPoint(QPoint(200, 100));
    QCOMPARE(polyline.pointCount(), 2);
}

void TestPolylineAnnotation::testUpdateLastPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));
    polyline.addPoint(QPoint(200, 100));

    polyline.updateLastPoint(QPoint(250, 150));

    QVector<QPoint> points = polyline.points();
    QCOMPARE(points.last(), QPoint(250, 150));
}

void TestPolylineAnnotation::testSetPoint()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    polyline.setPoint(1, QPoint(150, 200));

    QVector<QPoint> resultPoints = polyline.points();
    QCOMPARE(resultPoints[1], QPoint(150, 200));
}

void TestPolylineAnnotation::testRemoveLastPoint()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QCOMPARE(polyline.pointCount(), 4);

    polyline.removeLastPoint();
    QCOMPARE(polyline.pointCount(), 3);
}

void TestPolylineAnnotation::testMoveBy()
{
    QVector<QPoint> points = createTestPoints(3);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QVector<QPoint> originalPoints = polyline.points();
    polyline.moveBy(QPoint(50, 50));

    QVector<QPoint> movedPoints = polyline.points();
    for (int i = 0; i < originalPoints.size(); ++i) {
        QCOMPARE(movedPoints[i], originalPoints[i] + QPoint(50, 50));
    }
}

void TestPolylineAnnotation::testPointCount()
{
    PolylineAnnotation polyline(Qt::red, 3);
    QCOMPARE(polyline.pointCount(), 0);

    polyline.addPoint(QPoint(100, 100));
    QCOMPARE(polyline.pointCount(), 1);

    polyline.addPoint(QPoint(200, 100));
    polyline.addPoint(QPoint(300, 100));
    QCOMPARE(polyline.pointCount(), 3);
}

void TestPolylineAnnotation::testPoints()
{
    QVector<QPoint> points = createTestPoints(3);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QVector<QPoint> resultPoints = polyline.points();
    QCOMPARE(resultPoints.size(), points.size());
    for (int i = 0; i < points.size(); ++i) {
        QCOMPARE(resultPoints[i], points[i]);
    }
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestPolylineAnnotation::testBoundingRect_Empty()
{
    PolylineAnnotation polyline(Qt::red, 3);
    QVERIFY(polyline.boundingRect().isEmpty());
}

void TestPolylineAnnotation::testBoundingRect_SinglePoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));

    QRect rect = polyline.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

void TestPolylineAnnotation::testBoundingRect_MultiplePoints()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 50), QPoint(300, 150) };
    PolylineAnnotation polyline(points, Qt::red, 3);

    QRect rect = polyline.boundingRect();
    for (const QPoint& pt : points) {
        QVERIFY(rect.contains(pt));
    }
}

void TestPolylineAnnotation::testBoundingRect_AfterAddPoint()
{
    PolylineAnnotation polyline(Qt::red, 3);
    polyline.addPoint(QPoint(100, 100));

    QRect rect1 = polyline.boundingRect();

    polyline.addPoint(QPoint(300, 300));
    QRect rect2 = polyline.boundingRect();

    QVERIFY(rect2.width() > rect1.width() || rect2.height() > rect1.height());
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestPolylineAnnotation::testClone_CreatesNewInstance()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    auto cloned = polyline.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &polyline);
}

void TestPolylineAnnotation::testClone_PreservesPoints()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 3);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->pointCount(), polyline.pointCount());
    QCOMPARE(clonedPolyline->points(), polyline.points());
}

void TestPolylineAnnotation::testClone_PreservesColor()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::blue, 3);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->color(), polyline.color());
}

void TestPolylineAnnotation::testClone_PreservesStyle()
{
    QVector<QPoint> points = createTestPoints(4);
    PolylineAnnotation polyline(points, Qt::red, 5, LineEndStyle::BothArrow, LineStyle::Dashed);

    auto cloned = polyline.clone();
    auto* clonedPolyline = dynamic_cast<PolylineAnnotation*>(cloned.get());

    QVERIFY(clonedPolyline != nullptr);
    QCOMPARE(clonedPolyline->lineEndStyle(), LineEndStyle::BothArrow);
    QCOMPARE(clonedPolyline->width(), 5);
}

// ============================================================================
// Hit Testing Tests
// ============================================================================

void TestPolylineAnnotation::testContainsPoint_OnSegment()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point on the middle of the segment
    QVERIFY(polyline.containsPoint(QPoint(150, 100)));
}

void TestPolylineAnnotation::testContainsPoint_OffSegment()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point far from the segment
    QVERIFY(!polyline.containsPoint(QPoint(150, 300)));
}

void TestPolylineAnnotation::testContainsPoint_NearVertex()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100), QPoint(200, 200) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Point near a vertex
    QVERIFY(polyline.containsPoint(QPoint(200, 100)));
}

void TestPolylineAnnotation::testContainsPoint_MultipleSegments()
{
    QVector<QPoint> points = { QPoint(100, 100), QPoint(200, 100), QPoint(200, 200), QPoint(100, 200) };
    PolylineAnnotation polyline(points, Qt::red, 5);

    // Points on different segments
    QVERIFY(polyline.containsPoint(QPoint(150, 100)));  // First segment
    QVERIFY(polyline.containsPoint(QPoint(200, 150)));  // Second segment
    QVERIFY(polyline.containsPoint(QPoint(150, 200)));  // Third segment
}

// ============================================================================
// Style Accessors Tests
// ============================================================================

void TestPolylineAnnotation::testLineEndStyle()
{
    PolylineAnnotation polyline(Qt::red, 3, LineEndStyle::EndArrow);
    QCOMPARE(polyline.lineEndStyle(), LineEndStyle::EndArrow);

    polyline.setLineEndStyle(LineEndStyle::BothArrow);
    QCOMPARE(polyline.lineEndStyle(), LineEndStyle::BothArrow);
}

void TestPolylineAnnotation::testColor()
{
    PolylineAnnotation polyline(Qt::green, 3);
    QCOMPARE(polyline.color(), QColor(Qt::green));
}

void TestPolylineAnnotation::testWidth()
{
    PolylineAnnotation polyline(Qt::red, 8);
    QCOMPARE(polyline.width(), 8);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestPolylineAnnotation::testDraw_MultiplePoints()
{
    QVector<QPoint> points = createTestPoints(5, 30);
    PolylineAnnotation polyline(points, Qt::red, 3);

    QImage image(300, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    polyline.draw(painter);
    painter.end();

    // Verify something was drawn
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

namespace {
QImage renderPolyline(const PolylineAnnotation& polyline)
{
    QImage image(480, 480, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(240, 240);
    polyline.draw(painter);
    return image;
}
}

void TestPolylineAnnotation::testShortTerminalSegments_data()
{
    QTest::addColumn<int>("style");
    QTest::addColumn<int>("angle");
    QTest::addColumn<int>("length");
    for (int style = 0; style <= int(LineEndStyle::BothArrowOutline); ++style) {
        for (int angle : {0, 45, 90, 225}) {
            for (int length : {2, 20, 120}) {
                QTest::addRow("style-%d-angle-%d-length-%d", style, angle, length)
                    << style << angle << length;
            }
        }
    }
}

void TestPolylineAnnotation::testShortTerminalSegments()
{
    QFETCH(int, style);
    QFETCH(int, angle);
    QFETCH(int, length);
    QTransform rotation;
    rotation.rotate(angle);
    const QPoint start = rotation.map(QPoint(-length, 0));
    const QPoint end = -start;
    // Duplicate terminal vertices and arbitrarily short collinear segments must
    // not move either arrowhead or introduce a backwards tail.
    const QVector<QPoint> points{start, start, start / 2, QPoint(), end / 2, end, end};
    PolylineAnnotation split(points, Qt::red, 12, static_cast<LineEndStyle>(style));
    PolylineAnnotation simple({start, end}, Qt::red, 12, static_cast<LineEndStyle>(style));
    QCOMPARE(renderPolyline(split), renderPolyline(simple));
    QCOMPARE(split.points(), points);
    if (style != int(LineEndStyle::None)) {
        QVERIFY(split.containsPoint(end));
        if (style == int(LineEndStyle::BothArrow) || style == int(LineEndStyle::BothArrowOutline)) {
            QVERIFY(split.containsPoint(start));
        }
    }
    // The shaft must not run backwards beyond either endpoint on short lines.
    const QImage image = renderPolyline(simple);
    const QPointF direction = QPointF(end - start) / QLineF(start, end).length();
    const qreal tipProjection = QPointF::dotProduct(end, direction);
    const qreal startProjection = QPointF::dotProduct(start, direction);
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) == 0) continue;
            const QPointF pixel(x + 0.5 - 240, y + 0.5 - 240);
            const qreal projection = QPointF::dotProduct(pixel, direction);
            // Allow only the pen's round cap/outline radius and antialiasing.
            QVERIFY(projection <= tipProjection + 8);
            QVERIFY(projection >= startProjection - 8);
        }
    }
}

void TestPolylineAnnotation::testRepeatedAndZeroLengthPoints_data()
{
    QTest::addColumn<int>("style");
    QTest::addColumn<QVector<QPoint>>("points");
    for (int style = 0; style <= int(LineEndStyle::BothArrowOutline); ++style) {
        QTest::addRow("right-angle-%d", style) << style
            << QVector<QPoint>{QPoint(-100, 0), QPoint(0, 0), QPoint(0, 5)};
        QTest::addRow("acute-angle-%d", style) << style
            << QVector<QPoint>{QPoint(-100, 0), QPoint(0, 0), QPoint(-4, 3)};
        QTest::addRow("short-bend-%d", style) << style
            << QVector<QPoint>{QPoint(-2, 0), QPoint(0, 0), QPoint(0, 2)};
        QTest::addRow("zero-length-%d", style) << style
            << QVector<QPoint>{QPoint(0, 0), QPoint(0, 0)};
        QTest::addRow("return-to-start-%d", style) << style
            << QVector<QPoint>{QPoint(0, 0), QPoint(3, 0), QPoint(0, 0)};
    }
}

void TestPolylineAnnotation::testRepeatedAndZeroLengthPoints()
{
    QFETCH(int, style);
    QFETCH(QVector<QPoint>, points);
    QVector<QPoint> duplicates;
    for (const auto& point : points) duplicates << point << point;
    PolylineAnnotation simple(points, Qt::red, 8, static_cast<LineEndStyle>(style));
    PolylineAnnotation repeated(duplicates, Qt::red, 8, static_cast<LineEndStyle>(style));
    const QImage image = renderPolyline(simple);
    QCOMPARE(renderPolyline(repeated), image);
    QCOMPARE(repeated.points(), duplicates);
    if (points[0] == points[1]) {
        QImage empty(image.size(), image.format());
        empty.fill(Qt::transparent);
        QCOMPARE(image, empty);
        QVERIFY(!simple.containsPoint(points.first()));
    } else if (style != int(LineEndStyle::None)) {
        QVERIFY(simple.containsPoint(points.last()));
    }
}

void TestPolylineAnnotation::testHeadAtShortBentEndpoint_data()
{
    QTest::addColumn<int>("style");
    for (int style = int(LineEndStyle::EndArrow); style <= int(LineEndStyle::BothArrowOutline); ++style) {
        QTest::addRow("style-%d", style) << style;
    }
}

void TestPolylineAnnotation::testHeadAtShortBentEndpoint()
{
    QFETCH(int, style);
    PolylineAnnotation line({QPoint(-100, 0), QPoint(0, 0), QPoint(0, 5)},
                            Qt::red, 8, static_cast<LineEndStyle>(style));
    const bool filled = style == int(LineEndStyle::EndArrow) || style == int(LineEndStyle::BothArrow);
    // This wing area belongs to the head at (0, 5). It is outside both the
    // shaft and the old horizontal head at the preceding vertex (0, 0).
    const QPoint wing = filled ? QPoint(-9, 6) : QPoint(-9, 10);
    QVERIFY(qAlpha(renderPolyline(line).pixel(wing + QPoint(240, 240))) > 128);
    QVERIFY(line.containsPoint(wing));
}

QTEST_MAIN(TestPolylineAnnotation)
#include "tst_PolylineAnnotation.moc"
