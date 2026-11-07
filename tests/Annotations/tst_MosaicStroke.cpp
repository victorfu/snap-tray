#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <opencv2/imgproc.hpp>

#include "annotations/MosaicStroke.h"

#include "reference/LegacyMosaicStroke.h"
#include <QElapsedTimer>
#include <algorithm>
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

template <typename Stroke>
QImage renderStroke(const Stroke& stroke, const QSize& logicalSize, qreal targetDpr)
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
    void transformedParity();
    void incrementalParity_data();
    void incrementalParity();
    void incrementalWorkAndDuplicates();
    void boundedCache();
    void benchmark();
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



void TestMosaicStroke::transformedParity()
{
    auto source = std::make_shared<const QPixmap>(patternedSource(2.));
    for (const QVector<QPoint>& path : {QVector<QPoint>{{40, 20}, {95, 80}, {-3, 5}},
                                       QVector<QPoint>{{200, 200}, {220, 230}}}) {
        MosaicStroke actual(path, source, 12, 12);
        LegacyMosaicStroke expected(path, source, 12, 12);
        for (bool smooth : {false, true}) {
            for (auto mode : {QPainter::CompositionMode_SourceOver, QPainter::CompositionMode_Source}) {
                auto render = [&](const AnnotationItem& item) {
                    QImage image(600, 600, QImage::Format_ARGB32_Premultiplied);
                    image.setDevicePixelRatio(2.); image.fill(QColor(30, 40, 50));
                    QPainter painter(&image);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
                    painter.setCompositionMode(mode);
                    painter.setOpacity(.6);
                    painter.translate(30.25, 12.5); painter.scale(1.3, .9); painter.rotate(17);
                    item.draw(painter); painter.end(); return image;
                };
                QCOMPARE(render(actual), render(expected));
            }
        }
    }
}

void TestMosaicStroke::incrementalParity_data()
{
    QTest::addColumn<qreal>("sourceDpr");
    QTest::addColumn<qreal>("targetDpr");
    QTest::addColumn<int>("type");
    for (qreal source : {1., 1.25, 1.5, 2.}) {
        for (qreal target : {1., 1.25, 1.5, 2.}) {
            for (int type : {int(MosaicBlurType::Pixelate), int(MosaicBlurType::Gaussian)}) {
                QTest::newRow(qPrintable(QString("%1-%2-%3").arg(source).arg(target).arg(type)))
                    << source << target << type;
            }
        }
    }
}

void TestMosaicStroke::incrementalParity()
{
    QFETCH(qreal, sourceDpr);
    QFETCH(qreal, targetDpr);
    QFETCH(int, type);
    auto source = std::make_shared<const QPixmap>(patternedSource(sourceDpr));
    // Negative coordinates, image edges, tile boundaries, reversals, crossings,
    // duplicate endpoints and both growing and shrinking origins.
    const QVector<QPoint> path{{80, 65}, {20, 20}, {19, 19}, {19, 19},
        {90, 20}, {20, 80}, {90, 80}, {3, 2}, {-9, -11}, {119, 99},
        {121, 102}, {40, 60}, {80, 65}, {300, 200}};
    MosaicStroke actual({}, source, 12, 12, MosaicBlurType(type));
    LegacyMosaicStroke expected({}, source, 12, 12, MosaicBlurType(type));
    for (const auto& point : path) {
        actual.addPoint(point);
        expected.addPoint(point);
        QCOMPARE(actual.boundingRect(), expected.boundingRect());
        QCOMPARE(renderStroke(actual, QSize(340, 240), targetDpr),
                 renderStroke(expected, QSize(340, 240), targetDpr));
    }
    for (const QPointF delta : {QPointF(-11.5, 3.5), QPointF(50, -20)}) {
        actual.translate(delta);
        expected.translate(delta);
        QCOMPARE(renderStroke(actual, QSize(340, 240), targetDpr),
                 renderStroke(expected, QSize(340, 240), targetDpr));
    }
    // Growing only the maximum coordinates must reuse mask stamps, including
    // fractional source DPR, even when the final resampling needs full damage.
    if (MosaicBlurType(type) == MosaicBlurType::Pixelate) {
        const auto before = actual.renderWork();
        actual.addPoint({400, 250}); expected.addPoint({400, 250});
        QCOMPARE(renderStroke(actual, QSize(440, 280), targetDpr),
                 renderStroke(expected, QSize(440, 280), targetDpr));
        QCOMPARE(actual.renderWork().maskRebuilds, before.maskRebuilds);
    }
    const qreal replacementDpr = sourceDpr == 1.25 ? 2. : 1.25;
    source = std::make_shared<const QPixmap>(patternedSource(replacementDpr, true));
    actual.setSourcePixmap(source);
    expected.setSourcePixmap(source);
    QCOMPARE(renderStroke(actual, QSize(340, 240), targetDpr),
             renderStroke(expected, QSize(340, 240), targetDpr));
    auto copy = actual.clone();
    QCOMPARE(renderStroke(*copy, QSize(340, 240), targetDpr),
             renderStroke(expected, QSize(340, 240), targetDpr));

    // Alpha is deliberately replaced by the old binary mask, not multiplied.
    QPixmap translucent(150, 150);
    translucent.fill(QColor(80, 140, 210, 100));
    source = std::make_shared<const QPixmap>(translucent);
    actual.setSourcePixmap(source);
    expected.setSourcePixmap(source);
    QCOMPARE(renderStroke(actual, QSize(340, 240), targetDpr),
             renderStroke(expected, QSize(340, 240), targetDpr));
    actual.setSourcePixmap(nullptr);
    expected.setSourcePixmap(nullptr);
    QCOMPARE(renderStroke(actual, QSize(340, 240), targetDpr),
             renderStroke(expected, QSize(340, 240), targetDpr));
}

void TestMosaicStroke::incrementalWorkAndDuplicates()
{
    auto source = std::make_shared<const QPixmap>(patternedSource(2.));
    MosaicStroke stroke({{20, 20}}, source, 12, 12);
    stroke.addPoint({20, 20});
    QCOMPARE(stroke.points().size(), 2); // Preserve the first duplicate's dot.
    const auto first = renderStroke(stroke, kLogicalSize, 2.);
    const auto work = stroke.renderWork();
    stroke.addPoint({20, 20});
    QCOMPARE(stroke.points().size(), 2);
    QCOMPARE(renderStroke(stroke, kLogicalSize, 2.), first);
    QCOMPARE(stroke.renderWork().stamps, work.stamps);
    for (int i = 21; i < 90; ++i) {
        stroke.addPoint({i, i});
        renderStroke(stroke, kLogicalSize, 2.);
    }
    const auto before = stroke.renderWork();
    stroke.addPoint({90, 90});
    QVERIFY(stroke.previewAffectedBoundingRect().width() < stroke.boundingRect().width());
    renderStroke(stroke, kLogicalSize, 2.);
    QCOMPARE(stroke.renderWork().maskRebuilds, before.maskRebuilds);
    QCOMPARE(stroke.renderWork().stamps - before.stamps, quint64(2));
    QVERIFY(stroke.renderWork().sampledBlocks - before.sampledBlocks <= 9);
}

void TestMosaicStroke::boundedCache()
{
    auto source = std::make_shared<const QPixmap>(patternedSource(1.));
    MosaicStroke stroke({{0, 0}, {1, 1}}, source, 1100, 12);
    LegacyMosaicStroke reference({{0, 0}, {1, 1}}, source, 1100, 12);
    QCOMPARE(renderStroke(stroke, kLogicalSize, 1.), renderStroke(reference, kLogicalSize, 1.));
    QVERIFY(stroke.pixelateCacheBytes() <= MosaicStroke::kTileBudgetBytes + MosaicStroke::kBlockBudgetBytes);
    // Overflow must not retain a full bounding rectangle image.
    QVERIFY(stroke.estimatedRetainedBytes() < 4 * 1024 * 1024);
    stroke.setSourcePixmap(nullptr);
    QCOMPARE(stroke.pixelateCacheBytes(), std::size_t(0));
}

void TestMosaicStroke::benchmark()
{
    if (!qEnvironmentVariableIsSet("SNAPTRAY_MOSAIC_BENCHMARK"))
        QSKIP("Set SNAPTRAY_MOSAIC_BENCHMARK=1 for median/p95 comparison");
    QPixmap pixels(2600, 1600);
    pixels.fill(QColor(37, 123, 219));
    pixels.setDevicePixelRatio(2.);
    auto source = std::make_shared<const QPixmap>(pixels);
    for (int count : {25, 100, 200}) {
        QVector<QPoint> path;
        for (int i = 0; i < count; ++i) path.append(QPoint(40 + i * 5, 40 + i * 3));
        QVector<qint64> oldTimes, newTimes;
        MosaicStroke::RenderWork work;
        for (int iteration = 0; iteration < 21; ++iteration) {
            MosaicStroke current(path, source, 24, 12);
            LegacyMosaicStroke legacy(path, source, 24, 12);
            QImage output(2600, 1600, QImage::Format_ARGB32_Premultiplied);
            output.setDevicePixelRatio(2.); output.fill(Qt::transparent);
            QPainter painter(&output);
            current.draw(painter); legacy.draw(painter);
            const auto before = current.renderWork();
            const QPoint next = path.last() + QPoint(1, 1);
            current.addPoint(next); legacy.addPoint(next);
            QElapsedTimer timer;
            timer.start(); current.draw(painter); const auto currentNs = timer.nsecsElapsed();
            timer.restart(); legacy.draw(painter); const auto legacyNs = timer.nsecsElapsed();
            if (iteration) { newTimes.append(currentNs); oldTimes.append(legacyNs); }
            work = current.renderWork();
            work.stamps -= before.stamps; work.sampledBlocks -= before.sampledBlocks;
            work.tileWrites -= before.tileWrites;
        }
        std::sort(oldTimes.begin(), oldTimes.end()); std::sort(newTimes.begin(), newTimes.end());
        qInfo() << "points" << count << "legacy median/p95 ms" << oldTimes[10]/1e6 << oldTimes[18]/1e6
                << "tiled median/p95 ms" << newTimes[10]/1e6 << newTimes[18]/1e6
                << "new blocks/stamps/tile writes" << work.sampledBlocks << work.stamps << work.tileWrites;
    }
}

QTEST_MAIN(TestMosaicStroke)
#include "tst_MosaicStroke.moc"
