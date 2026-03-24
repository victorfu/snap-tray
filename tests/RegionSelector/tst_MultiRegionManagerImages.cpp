#include <QtTest>

#include <QImage>

#include "region/MultiRegionManager.h"

class tst_MultiRegionManagerImages : public QObject
{
    Q_OBJECT

private slots:
    void testMergeToSingleImage_UsesCoveringRectsForFractionalDpr();
    void testSeparateImages_UsesCoveringRectsForFractionalDpr();
};

namespace {

QPixmap makeFractionalDprBackground()
{
    QImage image(QSize(300, 120), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);

    for (int y = 0; y < image.height(); ++y) {
        image.setPixelColor(151, y, Qt::red);
        image.setPixelColor(152, y, Qt::green);
        image.setPixelColor(153, y, Qt::blue);
        image.setPixelColor(154, y, Qt::yellow);
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(1.5);
    return pixmap;
}

} // namespace

void tst_MultiRegionManagerImages::testMergeToSingleImage_UsesCoveringRectsForFractionalDpr()
{
    const QPixmap background = makeFractionalDprBackground();
    MultiRegionManager manager;
    manager.addRegion(QRect(101, 20, 1, 10));
    manager.addRegion(QRect(102, 20, 1, 10));

    const QImage merged = manager.mergeToSingleImage(background, 1.5);

    QVERIFY(!merged.isNull());
    QCOMPARE(merged.devicePixelRatio(), 1.5);
    QCOMPARE(merged.size(), QSize(4, 15));
    QCOMPARE(merged.pixelColor(0, 7), QColor(Qt::red));
    QCOMPARE(merged.pixelColor(1, 7), QColor(Qt::green));
    QCOMPARE(merged.pixelColor(2, 7), QColor(Qt::blue));
    QCOMPARE(merged.pixelColor(3, 7), QColor(Qt::yellow));
}

void tst_MultiRegionManagerImages::testSeparateImages_UsesCoveringRectsForFractionalDpr()
{
    const QPixmap background = makeFractionalDprBackground();
    MultiRegionManager manager;
    manager.addRegion(QRect(101, 20, 1, 10));
    manager.addRegion(QRect(102, 20, 1, 10));

    const QVector<QImage> images = manager.separateImages(background, 1.5);

    QCOMPARE(images.size(), 2);
    QVERIFY(!images[0].isNull());
    QVERIFY(!images[1].isNull());
    QCOMPARE(images[0].devicePixelRatio(), 1.5);
    QCOMPARE(images[1].devicePixelRatio(), 1.5);
    QCOMPARE(images[0].size(), QSize(2, 15));
    QCOMPARE(images[1].size(), QSize(2, 15));
    QCOMPARE(images[0].pixelColor(0, 7), QColor(Qt::red));
    QCOMPARE(images[0].pixelColor(1, 7), QColor(Qt::green));
    QCOMPARE(images[1].pixelColor(0, 7), QColor(Qt::blue));
    QCOMPARE(images[1].pixelColor(1, 7), QColor(Qt::yellow));
}

QTEST_MAIN(tst_MultiRegionManagerImages)
#include "tst_MultiRegionManagerImages.moc"
