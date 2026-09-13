#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <opencv2/imgproc.hpp>

#include "annotations/MosaicStroke.h"

#include <cmath>
#include <memory>

namespace {

const QSize kLogicalSize(120, 100);
constexpr int kBrushWidth = 12;
constexpr int kBlockSize = 12;

QRect physicalRect(const QRect& rect, qreal dpr)
{
    return QRect(qRound(rect.x() * dpr), qRound(rect.y() * dpr),
                 qRound(rect.width() * dpr), qRound(rect.height() * dpr));
}

void addPixelContentRows(bool includePixelate)
{
    QTest::addColumn<int>("blurType");
    QTest::addColumn<qreal>("sourceDpr");
    QTest::addColumn<qreal>("targetDpr");
    QTest::addColumn<QPoint>("start");
    QTest::addColumn<QPoint>("end");
    QTest::addColumn<QRect>("sampleRect");

    struct StrokeCase {
        const char* name;
        QPoint start;
        QPoint end;
        QRect sampleRect;
    };
    const StrokeCase cases[] = {
        {"interior", QPoint(30, 50), QPoint(90, 50), QRect(30, 45, 60, 10)},
        {"top-left", QPoint(2, 4), QPoint(40, 4), QRect(2, 1, 38, 8)},
        {"bottom-right", QPoint(80, 96), QPoint(118, 96), QRect(80, 91, 38, 8)},
    };
    for (const auto type : {MosaicBlurType::Gaussian, MosaicBlurType::Pixelate}) {
        if (!includePixelate && type == MosaicBlurType::Pixelate) {
            continue;
        }
        for (qreal sourceDpr : {1.0, 1.5, 2.0}) {
            for (qreal targetDpr : {1.0, 1.5, 2.0}) {
                for (const auto& strokeCase : cases) {
                    const QByteArray name = QStringLiteral("%1-%2-to-%3-%4")
                        .arg(type == MosaicBlurType::Gaussian ? "gaussian" : "pixelate")
                        .arg(sourceDpr).arg(targetDpr).arg(strokeCase.name).toLatin1();
                    QTest::newRow(name.constData())
                        << static_cast<int>(type) << sourceDpr << targetDpr
                        << strokeCase.start << strokeCase.end << strokeCase.sampleRect;
                }
            }
        }
    }
}

QPixmap patternedSource(qreal dpr, bool alternate = false)
{
    QImage image(physicalRect(QRect(QPoint(), kLogicalSize), dpr).size(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor((x * 17 + (alternate ? 90 : 0)) % 256,
                                             (y * 23) % 256, ((x + y) * 11) % 256));
        }
    }
    image.setDevicePixelRatio(dpr);
    return QPixmap::fromImage(image);
}

QImage gaussianReference(const QPixmap& source, const QRect& bounds, qreal targetDpr)
{
    const qreal sourceDpr = source.devicePixelRatio();
    const QRect clipped = physicalRect(bounds, sourceDpr).intersected(source.rect());
    QImage patch = source.toImage().copy(clipped).convertToFormat(QImage::Format_RGB32);
    cv::Mat input(patch.height(), patch.width(), CV_8UC4, patch.bits(), patch.bytesPerLine());
    cv::Mat blurred;
    cv::GaussianBlur(input, blurred, cv::Size(), qMax(1.0, qRound(kBlockSize * sourceDpr) / 2.0));

    // Assemble the reference in physical pixels, without the production patch
    // drawImage or stroke mask. Tests sample only the fully covered brush interior.
    QImage canvas(source.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    for (int y = 0; y < blurred.rows; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(blurred.ptr(y));
        for (int x = 0; x < blurred.cols; ++x) {
            canvas.setPixel(clipped.x() + x, clipped.y() + y, row[x]);
        }
    }

    QImage output(physicalRect(QRect(QPoint(), kLogicalSize), targetDpr).size(),
                  QImage::Format_ARGB32_Premultiplied);
    output.setDevicePixelRatio(targetDpr);
    output.fill(Qt::transparent);
    QPainter painter(&output);
    painter.drawImage(QRectF(QPointF(), QSizeF(kLogicalSize)), canvas, canvas.rect());
    painter.end();
    return output;
}

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
    void uniformColorSurvivesRendering_data();
    void uniformColorSurvivesRendering();
    void gaussianMatchesReference_data();
    void gaussianMatchesReference();
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

void TestMosaicStroke::uniformColorSurvivesRendering_data()
{
    addPixelContentRows(true);
}

void TestMosaicStroke::uniformColorSurvivesRendering()
{
    QFETCH(int, blurType);
    QFETCH(qreal, sourceDpr);
    QFETCH(qreal, targetDpr);
    QFETCH(QPoint, start);
    QFETCH(QPoint, end);
    QFETCH(QRect, sampleRect);

    const QColor color(20, 120, 220);
    QPixmap source(physicalRect(QRect(QPoint(), kLogicalSize), sourceDpr).size());
    source.fill(color);
    source.setDevicePixelRatio(sourceDpr);
    MosaicStroke stroke({start, end}, std::make_shared<const QPixmap>(source),
                        kBrushWidth, kBlockSize, static_cast<MosaicBlurType>(blurType));

    // Check the initial cache, reuse it across screens, then reuse it again.
    for (qreal dpr : {sourceDpr, targetDpr, targetDpr}) {
        const QImage rendered = renderStroke(stroke, kLogicalSize, dpr);
        const QRect samples = physicalRect(sampleRect, dpr);
        for (int y = samples.top(); y <= samples.bottom(); ++y) {
            for (int x = samples.left(); x <= samples.right(); ++x) {
                QCOMPARE(rendered.pixelColor(x, y), color);
            }
        }
        QVERIFY(!hasCoverageAt(rendered, QPoint(110, 10)));
    }
}

void TestMosaicStroke::gaussianMatchesReference_data()
{
    addPixelContentRows(false);
}

void TestMosaicStroke::gaussianMatchesReference()
{
    QFETCH(qreal, sourceDpr);
    QFETCH(qreal, targetDpr);
    QFETCH(QPoint, start);
    QFETCH(QPoint, end);
    QFETCH(QRect, sampleRect);

    auto source = std::make_shared<const QPixmap>(patternedSource(sourceDpr));
    MosaicStroke stroke({start, end}, source, kBrushWidth, kBlockSize, MosaicBlurType::Gaussian);
    for (qreal dpr : {sourceDpr, targetDpr, targetDpr}) {
        const QRect samples = physicalRect(sampleRect, dpr);
        const QImage expected = gaussianReference(*source, stroke.boundingRect(), dpr);
        QCOMPARE(renderStroke(stroke, kLogicalSize, dpr).copy(samples), expected.copy(samples));
    }

    // Replacing the source must discard both the previous pixels and cached DPR.
    const qreal replacementDpr = sourceDpr == 2.0 ? 1.0 : 2.0;
    source = std::make_shared<const QPixmap>(patternedSource(replacementDpr, true));
    stroke.setSourcePixmap(source);
    const QRect samples = physicalRect(sampleRect, targetDpr);
    const QImage expected = gaussianReference(*source, stroke.boundingRect(), targetDpr);
    QCOMPARE(renderStroke(stroke, kLogicalSize, targetDpr).copy(samples), expected.copy(samples));
}

QTEST_MAIN(TestMosaicStroke)
#include "tst_MosaicStroke.moc"
