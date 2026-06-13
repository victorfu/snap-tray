#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QtMath>

#include "annotation/AnnotationRenderHelper.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/MarkerStroke.h"

namespace {

QImage renderArrowVisual(bool useDirtyRegionRendering)
{
    AnnotationLayer layer;
    auto arrow = std::make_unique<ArrowAnnotation>(
        QPoint(40, 50),
        QPoint(160, 100),
        Qt::green,
        4);
    arrow->setControlPoint(QPoint(110, 20));
    layer.addItem(std::move(arrow));
    layer.setSelectedIndex(0);

    QImage image(240, 180, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    snaptray::annotation::SelectedAnnotationItems selectedItems;
    selectedItems.arrow = static_cast<ArrowAnnotation*>(layer.selectedItem());
    snaptray::annotation::drawAnnotationVisuals(
        painter,
        &layer,
        image.size(),
        1.0,
        QPoint(10, 8),
        useDirtyRegionRendering,
        selectedItems);
    painter.end();

    return image;
}

QImage renderMarkerVisual(bool useDirtyRegionRendering)
{
    constexpr qreal dpr = 1.5;
    const QSize logicalSize(240, 160);
    AnnotationLayer layer;
    QVector<QPointF> points = {
        QPointF(40.25, 70.5),
        QPointF(75.5, 44.25),
        QPointF(112.75, 92.5),
        QPointF(158.5, 58.75)
    };
    layer.addItem(std::make_unique<MarkerStroke>(points, Qt::yellow, 20));
    layer.setSelectedIndex(0);

    QImage image(qCeil(logicalSize.width() * dpr),
                 qCeil(logicalSize.height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    snaptray::annotation::SelectedAnnotationItems selectedItems;
    snaptray::annotation::drawAnnotationVisuals(
        painter,
        &layer,
        logicalSize,
        dpr,
        QPoint(10, 8),
        useDirtyRegionRendering,
        selectedItems);
    painter.end();

    return image;
}

} // namespace

class TestScreenCanvasAnnotationRenderHelper : public QObject
{
    Q_OBJECT

private slots:
    void testArrowRenderMatchesCachedAndDirtyPaths();
    void testMarkerRenderMatchesCachedAndDirtyPaths();
};

void TestScreenCanvasAnnotationRenderHelper::testArrowRenderMatchesCachedAndDirtyPaths()
{
    const QImage cachedImage = renderArrowVisual(false);
    const QImage dirtyImage = renderArrowVisual(true);

    QVERIFY(!cachedImage.isNull());
    QVERIFY(!dirtyImage.isNull());
    QCOMPARE(dirtyImage, cachedImage);
}

void TestScreenCanvasAnnotationRenderHelper::testMarkerRenderMatchesCachedAndDirtyPaths()
{
    const QImage cachedImage = renderMarkerVisual(false);
    const QImage dirtyImage = renderMarkerVisual(true);

    QVERIFY(!cachedImage.isNull());
    QVERIFY(!dirtyImage.isNull());
    QCOMPARE(dirtyImage, cachedImage);
}

QTEST_MAIN(TestScreenCanvasAnnotationRenderHelper)
#include "tst_AnnotationRenderHelper.moc"
