#include <QtTest>
#include "annotations/AnnotationGeometry.h"
#include "annotations/AnnotationStyle.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

class tst_AnnotationGeometry : public QObject
{
    Q_OBJECT
private slots:
    void shapeCopiesGeometryNotStyle();
    void arrowRestoresExactControlPoint();   // [RF 4]
    void polylineCopiesPoints();
    void textCopiesContentAndLayout();
    void emojiCopiesTransform();
    void mismatchedKindsAreRefused();
    void unsupportedKindsAreRefused();
    void equalMatchesCopy();
    void equalStyleComparesStyleOnly();
};

void tst_AnnotationGeometry::shapeCopiesGeometryNotStyle()
{
    ShapeAnnotation from(QRect(10, 10, 50, 40), ShapeType::Rectangle, Qt::red, 3, false);
    from.setRotation(30);
    from.setScale(1.5, 2.0);
    from.moveBy(QPointF(0.5, 0.25));
    ShapeAnnotation to(QRect(0, 0, 5, 5), ShapeType::Rectangle, Qt::blue, 9, true);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.rectF(), from.rectF());
    QCOMPARE(to.rotation(), from.rotation());
    QCOMPARE(to.scaleX(), 1.5);
    QCOMPARE(to.scaleY(), 2.0);
    QCOMPARE(to.color(), QColor(Qt::blue));   // style untouched
    QCOMPARE(to.width(), 9);
    QVERIFY(to.filled());
}

void tst_AnnotationGeometry::arrowRestoresExactControlPoint()
{
    ArrowAnnotation from(QPoint(0, 0), QPoint(100, 0), Qt::red, 3);
    from.setControlPoint(QPointF(50, 40));
    ArrowAnnotation to(QPoint(10, 10), QPoint(20, 90), Qt::red, 3);
    to.setControlPoint(QPointF(-7, 3));
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.start(), QPoint(0, 0));
    QCOMPARE(to.end(), QPoint(100, 0));
    QCOMPARE(to.controlPoint(), QPointF(50, 40));
}

void tst_AnnotationGeometry::polylineCopiesPoints()
{
    PolylineAnnotation from(QVector<QPoint>{{0, 0}, {10, 10}, {20, 0}}, Qt::red, 3);
    PolylineAnnotation to(QVector<QPoint>{{5, 5}, {6, 6}}, Qt::green, 7);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.points(), from.points());
    QCOMPARE(to.width(), 7);
}

void tst_AnnotationGeometry::textCopiesContentAndLayout()
{
    QFont small;
    small.setPointSize(10);
    QFont big;
    big.setPointSize(30);
    TextBoxAnnotation from(QPointF(40, 50), QStringLiteral("Before"), small, Qt::red);
    from.setWrapWidth(120);
    from.setRotation(15);
    from.setScale(1.25);
    from.setMirror(true, false);
    TextBoxAnnotation to(QPointF(0, 0), QStringLiteral("After, longer"), big, Qt::blue);
    // Style first (as undo does), then geometry: the box must be from's, not
    // recomputed from `to`'s old text.
    QVERIFY(AnnotationStyle::copyStyle(from, to));
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.text(), QStringLiteral("Before"));
    QCOMPARE(to.position(), QPointF(40, 50));
    QCOMPARE(to.box(), from.box());
    QCOMPARE(to.wrapWidth(), from.wrapWidth());
    QCOMPARE(to.rotation(), 15.0);
    QCOMPARE(to.scale(), 1.25);
    QVERIFY(to.mirrorX());
    // Geometry alone keeps to's font.
    TextBoxAnnotation other(QPointF(0, 0), QStringLiteral("x"), big, Qt::blue);
    QVERIFY(AnnotationGeometry::copy(from, other));
    QCOMPARE(other.font().pointSize(), 30);
}

void tst_AnnotationGeometry::emojiCopiesTransform()
{
    EmojiStickerAnnotation from(QPoint(30, 40), QStringLiteral("🙂"), 1.5);
    from.setRotation(45);
    from.setMirror(false, true);
    EmojiStickerAnnotation to(QPoint(0, 0), QStringLiteral("🙂"), 1.0);
    QVERIFY(AnnotationGeometry::copy(from, to));
    QCOMPARE(to.position(), QPoint(30, 40));
    QCOMPARE(to.scale(), 1.5);
    QCOMPARE(to.rotation(), 45.0);
    QVERIFY(to.mirrorY());
}

void tst_AnnotationGeometry::mismatchedKindsAreRefused()
{
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::red, 3, false);
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 3);
    QVERIFY(!AnnotationGeometry::copy(shape, arrow));
    QVERIFY(!AnnotationGeometry::equal(shape, arrow));
}

void tst_AnnotationGeometry::unsupportedKindsAreRefused()
{
    StepBadgeAnnotation a(QPoint(0, 0), Qt::red, 1);
    StepBadgeAnnotation b(QPoint(5, 5), Qt::red, 2);
    QVERIFY(!AnnotationGeometry::supports(a));
    QVERIFY(!AnnotationGeometry::copy(a, b));
    QCOMPARE(b.position(), QPoint(5, 5));
}

void tst_AnnotationGeometry::equalMatchesCopy()
{
    ShapeAnnotation a(QRect(0, 0, 10, 10), ShapeType::Ellipse, Qt::red, 3, false);
    ShapeAnnotation b(QRect(4, 4, 10, 10), ShapeType::Ellipse, Qt::blue, 3, false);
    QVERIFY(!AnnotationGeometry::equal(a, b));
    QVERIFY(AnnotationGeometry::copy(a, b));
    QVERIFY(AnnotationGeometry::equal(a, b));   // colour differs, geometry equal
}

void tst_AnnotationGeometry::equalStyleComparesStyleOnly()
{
    ArrowAnnotation a(QPoint(0, 0), QPoint(10, 0), Qt::red, 3);
    ArrowAnnotation b(QPoint(50, 50), QPoint(90, 0), Qt::red, 3);
    QVERIFY(AnnotationStyle::equalStyle(a, b));
    b.setWidth(4);
    QVERIFY(!AnnotationStyle::equalStyle(a, b));
}

QTEST_MAIN(tst_AnnotationGeometry)
#include "tst_AnnotationGeometry.moc"
