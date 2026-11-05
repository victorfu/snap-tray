#include <QtTest/QtTest>

#include "annotations/AnnotationStyle.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

class tst_AnnotationStyle : public QObject
{
    Q_OBJECT
private slots:
    void readsEachKind();
    void appliesSupportedProperties();
    void ignoresUnsupportedAndUnchanged();
};

void tst_AnnotationStyle::readsEachKind()
{
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 4, LineEndStyle::BothArrow,
                          LineStyle::Dotted);
    const auto a = AnnotationStyle::read(arrow);
    QCOMPARE(a.kind, StyleTargetKind::Arrow);
    QCOMPARE(a.color, QColor(Qt::red));
    QCOMPARE(a.width, 4);
    QCOMPARE(a.arrowStyle, int(LineEndStyle::BothArrow));
    QCOMPARE(a.lineStyle, int(LineStyle::Dotted));
    PolylineAnnotation line(QVector<QPoint>{{0, 0}, {5, 5}}, Qt::red, 2);
    QCOMPARE(AnnotationStyle::read(line).kind, StyleTargetKind::Polyline);
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Ellipse, Qt::green, 2, true);
    QCOMPARE(AnnotationStyle::read(shape).kind, StyleTargetKind::Shape);
    QVERIFY(AnnotationStyle::read(shape).filled);
    QCOMPARE(AnnotationStyle::read(shape).shapeType, int(ShapeType::Ellipse));
    QFont font(QStringLiteral("Helvetica"), 18);
    font.setBold(true);
    TextBoxAnnotation text(QPointF(0, 0), QStringLiteral("Hi"), font, Qt::black);
    const auto t = AnnotationStyle::read(text);
    QCOMPARE(t.kind, StyleTargetKind::Text);
    QVERIFY(t.font.bold());
    QCOMPARE(t.font.pointSize(), 18);
    EmojiStickerAnnotation emoji(QPoint(0, 0), QStringLiteral("x"));
    QCOMPARE(AnnotationStyle::kindOf(emoji), StyleTargetKind::None);
    QCOMPARE(AnnotationStyle::read(emoji).kind, StyleTargetKind::None);
}

void tst_AnnotationStyle::appliesSupportedProperties()
{
    ArrowAnnotation arrow(QPoint(0, 0), QPoint(10, 0), Qt::red, 4);
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::Color, QColor(Qt::blue)));
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::Width, 9));
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::ArrowStyle, int(LineEndStyle::None)));
    QVERIFY(AnnotationStyle::apply(arrow, StyleProperty::LineStyle, int(LineStyle::Dashed)));
    QCOMPARE(arrow.color(), QColor(Qt::blue));
    QCOMPARE(arrow.width(), 9);
    QCOMPARE(arrow.lineEndStyle(), LineEndStyle::None);
    QCOMPARE(arrow.lineStyle(), LineStyle::Dashed);
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::green, 2);
    QVERIFY(AnnotationStyle::apply(shape, StyleProperty::Fill, true));
    QVERIFY(shape.filled());
    TextBoxAnnotation text(QPointF(0, 0), QStringLiteral("Hi"), QFont(QStringLiteral("Helvetica"), 12),
                           Qt::black);
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::FontSize, 24));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::Italic, true));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::Bold, true));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::Underline, true));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::FontFamily, QStringLiteral("Courier")));
    QVERIFY(AnnotationStyle::apply(text, StyleProperty::Color, QColor(Qt::red)));
    QCOMPARE(text.font().pointSize(), 24);
    QVERIFY(text.font().italic());
    QVERIFY(text.font().bold());
    QVERIFY(text.font().underline());
    QCOMPARE(text.font().family(), QStringLiteral("Courier"));
    QCOMPARE(text.color(), QColor(Qt::red));
}

void tst_AnnotationStyle::ignoresUnsupportedAndUnchanged()
{
    ShapeAnnotation shape(QRect(0, 0, 10, 10), ShapeType::Rectangle, Qt::green, 2);
    QVERIFY(!AnnotationStyle::apply(shape, StyleProperty::ArrowStyle, 1));
    QVERIFY(!AnnotationStyle::apply(shape, StyleProperty::Color, QColor(Qt::green)));
    QVERIFY(!AnnotationStyle::apply(shape, StyleProperty::Width, 2));
    QVERIFY(!AnnotationStyle::supports(StyleTargetKind::None, StyleProperty::Color));
    QVERIFY(!AnnotationStyle::supports(StyleTargetKind::Text, StyleProperty::Width));
    QVERIFY(AnnotationStyle::supports(StyleTargetKind::Polyline, StyleProperty::ArrowStyle));
    EmojiStickerAnnotation emoji(QPoint(0, 0), QStringLiteral("x"));
    QVERIFY(!AnnotationStyle::apply(emoji, StyleProperty::Color, QColor(Qt::red)));
}

QTEST_MAIN(tst_AnnotationStyle)
#include "tst_AnnotationStyle.moc"
