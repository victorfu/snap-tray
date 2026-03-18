#include <QtTest/QtTest>

#include "region/RegionExportManager.h"

namespace {

QPixmap makeHighDpiPixmap()
{
    QImage image(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(10 + x * 20, 15 + y * 17, 30 + (x + y) * 9));
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(2.0);
    return pixmap;
}

} // namespace

class tst_RegionExportManager : public QObject
{
    Q_OBJECT

private slots:
    void testPrepareExport_NormalizesHighDpiCrop();
};

void tst_RegionExportManager::testPrepareExport_NormalizesHighDpiCrop()
{
    const QPixmap background = makeHighDpiPixmap();
    const QImage sourceImage = background.toImage();

    RegionExportManager manager;
    manager.setBackgroundPixmap(background);
    manager.setDevicePixelRatio(2.0);

    const RegionExportManager::PreparedExport prepared = manager.prepareExport(QRect(1, 1, 2, 2), 0);
    QVERIFY(prepared.isValid());
    QCOMPARE(prepared.pixmap.devicePixelRatio(), 2.0);
    QCOMPARE(prepared.image.devicePixelRatio(), 1.0);
    QCOMPARE(prepared.image.size(), QSize(4, 4));

    for (int y = 0; y < prepared.image.height(); ++y) {
        for (int x = 0; x < prepared.image.width(); ++x) {
            QCOMPARE(prepared.image.pixelColor(x, y), sourceImage.pixelColor(x + 2, y + 2));
        }
    }
}

QTEST_MAIN(tst_RegionExportManager)
#include "tst_RegionExportManager.moc"
