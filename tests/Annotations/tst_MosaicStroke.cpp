#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QPixmap>

#include "annotations/MosaicStroke.h"

#include <cmath>
#include <memory>

namespace {

QImage renderStroke(const MosaicStroke& stroke, const QSize& logicalSize, qreal targetDpr)
{
    const QSize physicalSize(
        qCeil(logicalSize.width() * targetDpr),
        qCeil(logicalSize.height() * targetDpr));
    QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(targetDpr);

    QPainter painter(&image);
    stroke.draw(painter);
    painter.end();
    return image;
}

bool hasCoverageAt(const QImage& image, const QPoint& logicalPoint)
{
    const qreal dpr = image.devicePixelRatio();
    const QPoint physicalPoint(
        qFloor(logicalPoint.x() * dpr),
        qFloor(logicalPoint.y() * dpr));
    const QRect sampleRect(
        physicalPoint - QPoint(1, 1),
        QSize(3, 3));
    const QRect clipped = sampleRect.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            if (image.pixelColor(x, y).alpha() > 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

class TestMosaicStroke : public QObject
{
    Q_OBJECT

private slots:
    void coverageSurvivesTargetDprChange_data();
    void coverageSurvivesTargetDprChange();
};

void TestMosaicStroke::coverageSurvivesTargetDprChange_data()
{
    QTest::addColumn<int>("blurType");
    QTest::addColumn<qreal>("sourceDpr");
    QTest::addColumn<qreal>("targetDpr");
    QTest::newRow("pixelate-1x-to-2x")
        << static_cast<int>(MosaicStroke::BlurType::Pixelate) << 1.0 << 2.0;
    QTest::newRow("pixelate-2x-to-1x")
        << static_cast<int>(MosaicStroke::BlurType::Pixelate) << 2.0 << 1.0;
    QTest::newRow("gaussian-1x-to-2x")
        << static_cast<int>(MosaicStroke::BlurType::Gaussian) << 1.0 << 2.0;
    QTest::newRow("gaussian-2x-to-1x")
        << static_cast<int>(MosaicStroke::BlurType::Gaussian) << 2.0 << 1.0;
}

void TestMosaicStroke::coverageSurvivesTargetDprChange()
{
    QFETCH(int, blurType);
    QFETCH(qreal, sourceDpr);
    QFETCH(qreal, targetDpr);

    QPixmap source(
        qCeil(48 * sourceDpr),
        qCeil(40 * sourceDpr));
    source.fill(QColor(20, 120, 220));
    source.setDevicePixelRatio(sourceDpr);
    auto sharedSource = std::make_shared<const QPixmap>(source);

    const QVector<QPoint> points{QPoint(10, 20), QPoint(30, 20)};
    MosaicStroke stroke(
        points,
        sharedSource,
        4,
        4,
        static_cast<MosaicStroke::BlurType>(blurType));

    // Prime the cache at the source DPR, then render on a destination with a
    // different DPR as happens when a pin crosses monitors.
    const QImage atSourceDpr = renderStroke(stroke, QSize(48, 40), sourceDpr);
    const QImage atTargetDpr = renderStroke(stroke, QSize(48, 40), targetDpr);

    QVERIFY(hasCoverageAt(atSourceDpr, points.constFirst()));
    QVERIFY(hasCoverageAt(atSourceDpr, points.constLast()));
    QVERIFY(hasCoverageAt(atTargetDpr, points.constFirst()));
    QVERIFY(hasCoverageAt(atTargetDpr, points.constLast()));
    QVERIFY(!hasCoverageAt(atTargetDpr, QPoint(35, 27)));
    QVERIFY(!hasCoverageAt(atTargetDpr, QPoint(46, 2)));
}

QTEST_MAIN(TestMosaicStroke)
#include "tst_MosaicStroke.moc"
