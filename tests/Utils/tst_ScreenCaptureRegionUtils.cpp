#include <QtTest/QtTest>

#include <QImage>

#include "utils/ScreenCaptureRegionUtils.h"

class tst_ScreenCaptureRegionUtils : public QObject
{
    Q_OBJECT

private slots:
    void testCropLogicalRegionPreservesHiDpiMetadata();
    void testCropLogicalRegionRejectsOutOfBoundsRegion();
    void testCropLogicalRegionUsesCoveringRectForFractionalDpr();
};

namespace {

QPixmap makePixmap(const QSize& physicalSize, qreal dpr)
{
    QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    return QPixmap::fromImage(image);
}

} // namespace

void tst_ScreenCaptureRegionUtils::testCropLogicalRegionPreservesHiDpiMetadata()
{
    QImage image(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(20 + x * 10, 30 + y * 10, 90));
        }
    }

    QPixmap screenshot = QPixmap::fromImage(image);
    screenshot.setDevicePixelRatio(2.0);

    const auto result =
        SnapTray::ScreenCaptureRegionUtils::cropLogicalRegionFromScreenshot(
            screenshot,
            QRect(1, 1, 2, 2));

    QVERIFY(result.isValid());
    QCOMPARE(result.devicePixelRatio, 2.0);
    QCOMPARE(result.logicalScreenSize, QSize(4, 4));
    QCOMPARE(result.pixmap.devicePixelRatio(), 2.0);
    QCOMPARE(result.pixmap.size(), QSize(4, 4));
    QCOMPARE(result.pixmap.toImage().pixelColor(0, 0), image.pixelColor(2, 2));
}

void tst_ScreenCaptureRegionUtils::testCropLogicalRegionRejectsOutOfBoundsRegion()
{
    QPixmap screenshot(200, 100);
    screenshot.fill(Qt::white);

    const auto result =
        SnapTray::ScreenCaptureRegionUtils::cropLogicalRegionFromScreenshot(
            screenshot,
            QRect(199, 0, 2, 2));

    QVERIFY(!result.isValid());
    QVERIFY(result.error.contains(QStringLiteral("exceeds logical screen bounds")));
}

void tst_ScreenCaptureRegionUtils::testCropLogicalRegionUsesCoveringRectForFractionalDpr()
{
    QImage image(QSize(300, 150), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::blue);
    for (int y = 0; y < image.height(); ++y) {
        image.setPixelColor(151, y, Qt::red);
        image.setPixelColor(152, y, Qt::blue);
        image.setPixelColor(153, y, Qt::blue);
    }

    QPixmap screenshot = QPixmap::fromImage(image);
    screenshot.setDevicePixelRatio(1.5);

    const auto result =
        SnapTray::ScreenCaptureRegionUtils::cropLogicalRegionFromScreenshot(
            screenshot,
            QRect(101, 20, 1, 10));

    QVERIFY(result.isValid());
    const QColor leftPixel = result.pixmap.toImage().pixelColor(0, result.pixmap.height() / 2);
    QVERIFY2(leftPixel.red() > leftPixel.blue(),
             "Expected covering crop to preserve the left fractional-DPR edge pixel.");
}

QTEST_MAIN(tst_ScreenCaptureRegionUtils)
#include "tst_ScreenCaptureRegionUtils.moc"
