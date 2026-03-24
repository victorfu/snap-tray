#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>

#include "annotation/AnnotationRenderHelper.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"

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

} // namespace

class TestScreenCanvasAnnotationRenderHelper : public QObject
{
    Q_OBJECT

private slots:
    void testArrowRenderMatchesCachedAndDirtyPaths();
};

void TestScreenCanvasAnnotationRenderHelper::testArrowRenderMatchesCachedAndDirtyPaths()
{
    const QImage cachedImage = renderArrowVisual(false);
    const QImage dirtyImage = renderArrowVisual(true);

    QVERIFY(!cachedImage.isNull());
    QVERIFY(!dirtyImage.isNull());
    QCOMPARE(dirtyImage, cachedImage);
}

QTEST_MAIN(TestScreenCanvasAnnotationRenderHelper)
#include "tst_AnnotationRenderHelper.moc"
