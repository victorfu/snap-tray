#include <QtTest/QtTest>

#include "history/AnnotationSerializer.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/PencilStroke.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/StepBadgeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

#include <QFont>
#include <QImage>
#include <QJsonDocument>

namespace {

SharedPixmap makeSharedPixmap()
{
    QPixmap pixmap(240, 160);
    pixmap.fill(Qt::white);
    return std::make_shared<const QPixmap>(pixmap);
}

void populateLayer(AnnotationLayer* layer, const SharedPixmap& sourcePixmap)
{
    if (!layer) {
        return;
    }

    auto text = std::make_unique<TextBoxAnnotation>(QPointF(12.0, 18.0),
                                                    QStringLiteral("hello"),
                                                    QFont(QStringLiteral("Helvetica"), 14),
                                                    QColor(20, 30, 40));
    text->setRotation(15.0);
    text->setScale(1.25);
    text->setMirror(true, false);
    text->setBox(QRectF(0.0, 0.0, 140.0, 44.0));
    layer->addItem(std::move(text));

    auto shape = std::make_unique<ShapeAnnotation>(QRect(24, 36, 60, 48),
                                                   ShapeType::Ellipse,
                                                   QColor(200, 0, 0),
                                                   4,
                                                   true);
    shape->setRotation(22.0);
    shape->setScale(1.1, 0.9);
    layer->addItem(std::move(shape));

    auto arrow = std::make_unique<ArrowAnnotation>(QPoint(20, 20),
                                                   QPoint(120, 40),
                                                   QColor(0, 120, 255),
                                                   5,
                                                   LineEndStyle::BothArrowOutline,
                                                   LineStyle::Dashed);
    arrow->setControlPoint(QPoint(70, 10));
    layer->addItem(std::move(arrow));

    layer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(10, 100), QPoint(40, 110), QPoint(80, 96)},
        QColor(0, 180, 120),
        3,
        LineEndStyle::EndArrowLine,
        LineStyle::Dotted));

    layer->addItem(std::make_unique<PencilStroke>(
        QVector<QPointF>{QPointF(8.0, 8.0), QPointF(20.0, 14.0), QPointF(38.0, 26.0)},
        QColor(80, 40, 220),
        6,
        LineStyle::Dashed));

    layer->addItem(std::make_unique<MarkerStroke>(
        QVector<QPointF>{QPointF(30.0, 70.0), QPointF(48.0, 88.0), QPointF(66.0, 90.0)},
        QColor(255, 255, 0, 180),
        14));

    layer->addItem(std::make_unique<MosaicRectAnnotation>(
        QRect(100, 70, 40, 30),
        sourcePixmap,
        10,
        MosaicBlurType::Gaussian));

    layer->addItem(std::make_unique<MosaicStroke>(
        QVector<QPoint>{QPoint(150, 20), QPoint(168, 30), QPoint(176, 44)},
        sourcePixmap,
        18,
        9,
        MosaicBlurType::Pixelate));

    auto badge = std::make_unique<StepBadgeAnnotation>(QPoint(180, 110),
                                                       QColor(255, 80, 20),
                                                       3,
                                                       StepBadgeAnnotation::kBadgeRadiusLarge);
    badge->setRotation(12.0);
    badge->setMirror(true, true);
    layer->addItem(std::move(badge));

    auto emoji = std::make_unique<EmojiStickerAnnotation>(QPoint(210, 50),
                                                          QStringLiteral("📌"),
                                                          1.4);
    emoji->setRotation(35.0);
    emoji->setMirror(false, true);
    layer->addItem(std::move(emoji));
}

} // namespace

class tst_AnnotationSerializer : public QObject
{
    Q_OBJECT

private slots:
    void testRoundtripPreservesSupportedAnnotations();
};

void tst_AnnotationSerializer::testRoundtripPreservesSupportedAnnotations()
{
    const SharedPixmap sourcePixmap = makeSharedPixmap();
    AnnotationLayer original;
    populateLayer(&original, sourcePixmap);
    const QByteArray serialized = SnapTray::serializeAnnotationLayer(original);
    QVERIFY(!serialized.isEmpty());

    AnnotationLayer restored;
    QString errorMessage;
    QVERIFY2(SnapTray::deserializeAnnotationLayer(serialized, &restored, sourcePixmap, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(restored.itemCount(), original.itemCount());

    const QByteArray roundtrip = SnapTray::serializeAnnotationLayer(restored);
    QCOMPARE(QJsonDocument::fromJson(roundtrip), QJsonDocument::fromJson(serialized));
}

QTEST_MAIN(tst_AnnotationSerializer)
#include "tst_AnnotationSerializer.moc"
