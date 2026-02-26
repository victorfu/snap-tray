#include <QtTest/QtTest>
#include <QPainter>

#include "detection/ScrollSeamBlender.h"
#include "scroll/ScrollStitchEngine.h"

namespace {

QImage makeContentImage(int width, int height, int seed)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int r = (y * 7 + x * 3 + seed * 17) % 256;
            const int g = (y * 5 + x * 11 + seed * 29) % 256;
            const int b = (y * 13 + x * 2 + seed * 31) % 256;
            line[x] = qRgba(r, g, b, 255);
        }
    }
    return image;
}

QImage makeFrame(const QImage &content,
                 int offsetY,
                 int frameHeight,
                 int stickyHeaderPx = 0,
                 int stickyFooterPx = 0,
                 int rightRailPx = 0)
{
    const int copyY = qBound(0, offsetY, qMax(0, content.height() - frameHeight));
    QImage frame = content.copy(0, copyY, content.width(), frameHeight)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QPainter painter(&frame);
    if (stickyHeaderPx > 0) {
        painter.fillRect(0, 0, frame.width(), stickyHeaderPx, QColor(31, 41, 55, 255));
    }
    if (stickyFooterPx > 0) {
        painter.fillRect(0, frame.height() - stickyFooterPx, frame.width(), stickyFooterPx,
                         QColor(15, 23, 42, 255));
    }
    if (rightRailPx > 0) {
        painter.fillRect(frame.width() - rightRailPx, 0, rightRailPx, frame.height(),
                         QColor(96, 96, 96, 255));
    }
    painter.end();

    return frame;
}

} // namespace

class tst_ScrollStitchEngine : public QObject
{
    Q_OBJECT

private slots:
    void testGoodAppendUsesTemplateNccStrategy();
    void testNoChangeGateRequiresBothLowDiffSignals();
    void testLowConfidenceFrameIsRejectedAndNotCommitted();
    void testTemporalMaskHandlesStickyRegions();
    void testAutoIgnoreBottomEdgeToggleAffectsBottomIgnoreMetric();
    void testRepeatedListLikeFramesAccumulateHeight();
    void testLargeFalseAppendGetsRejected();
    void testSeamBlenderFindsLowEnergyRow();
    void testMaxOutputHeightLimitIsEnforced();
    void testUndoRestoresPreviousHeight();
    void testComposeFinalIsOpaque();
};

void tst_ScrollStitchEngine::testGoodAppendUsesTemplateNccStrategy()
{
    const QImage content = makeContentImage(320, 2400, 1);

    ScrollStitchOptions options;
    options.goodThreshold = 0.90;
    options.partialThreshold = 0.80;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const StitchFrameResult result = engine.append(makeFrame(content, 44, 240));
    QVERIFY(result.quality == StitchQuality::Good || result.quality == StitchQuality::PartiallyGood);
    QCOMPARE(result.strategy, StitchMatchStrategy::TemplateNcc1D);
    QVERIFY(!result.rejectedByConfidence);
    QVERIFY(result.appendHeight > 0);
    QVERIFY(engine.composeFinal().height() > 240);
}

void tst_ScrollStitchEngine::testNoChangeGateRequiresBothLowDiffSignals()
{
    QImage content(320, 2600, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < content.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(content.scanLine(y));
        for (int x = 0; x < content.width(); ++x) {
            if (x > 96 && x < 224) {
                line[x] = qRgba(200, 200, 200, 255);
            } else {
                const int v = (y * 19 + x * 7) % 255;
                line[x] = qRgba(v, (v * 3) % 255, (v * 5) % 255, 255);
            }
        }
    }

    ScrollStitchEngine engine;
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    const StitchFrameResult result = engine.append(makeFrame(content, 40, 240));
    QVERIFY(result.quality != StitchQuality::NoChange);
}

void tst_ScrollStitchEngine::testLowConfidenceFrameIsRejectedAndNotCommitted()
{
    const QImage content = makeContentImage(320, 2400, 2);

    ScrollStitchEngine engine;
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const int heightBefore = engine.composeFinal().height();
    QImage unrelated(320, 240, QImage::Format_ARGB32_Premultiplied);
    unrelated.fill(QColor(17, 22, 31, 255));

    const StitchFrameResult result = engine.append(unrelated);
    QCOMPARE(result.quality, StitchQuality::Bad);
    QVERIFY(result.rejectedByConfidence);
    QCOMPARE(result.appendHeight, 0);
    QCOMPARE(engine.composeFinal().height(), heightBefore);
}

void tst_ScrollStitchEngine::testTemporalMaskHandlesStickyRegions()
{
    const QImage content = makeContentImage(360, 3600, 7);

    ScrollStitchOptions options;
    options.goodThreshold = 0.86;
    options.partialThreshold = 0.72;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 260, 20, 14, 20)));

    const StitchFrameResult r1 = engine.append(makeFrame(content, 42, 260, 20, 14, 20));
    const StitchFrameResult r2 = engine.append(makeFrame(content, 84, 260, 20, 14, 20));
    const StitchFrameResult r3 = engine.append(makeFrame(content, 126, 260, 20, 14, 20));

    QVERIFY(r1.quality != StitchQuality::Bad);
    QVERIFY(r2.quality != StitchQuality::Bad);
    QVERIFY(r3.quality != StitchQuality::Bad);
    QVERIFY(r3.stickyHeaderPx > 0);
    QVERIFY(r3.stickyFooterPx > 0);
    QVERIFY(r3.dynamicMaskCoverage > 0.0);
}

void tst_ScrollStitchEngine::testAutoIgnoreBottomEdgeToggleAffectsBottomIgnoreMetric()
{
    const QImage content = makeContentImage(360, 3600, 19);
    const int frameHeight = 260;
    const int unstableFooterPx = 22;

    auto frameWithUnstableFooter = [&](int offsetY, int phase) {
        QImage frame = makeFrame(content, offsetY, frameHeight, 0, 0, 0);
        QPainter painter(&frame);
        painter.fillRect(0, frame.height() - unstableFooterPx, frame.width(), unstableFooterPx,
                         QColor((phase * 61) % 255, (phase * 37) % 255, (phase * 19) % 255, 255));
        painter.end();
        return frame;
    };

    ScrollStitchOptions enabledOptions;
    enabledOptions.goodThreshold = 0.86;
    enabledOptions.partialThreshold = 0.72;
    enabledOptions.minScrollAmount = 8;
    enabledOptions.autoIgnoreBottomEdge = true;

    ScrollStitchEngine enabledEngine(enabledOptions);
    QVERIFY(enabledEngine.start(frameWithUnstableFooter(0, 1)));
    const StitchFrameResult enabledR1 = enabledEngine.append(frameWithUnstableFooter(0, 2));
    const StitchFrameResult enabledR2 = enabledEngine.append(frameWithUnstableFooter(0, 3));
    QVERIFY(enabledR1.bottomIgnorePx >= 0);
    QVERIFY(enabledR2.bottomIgnorePx > 0);

    ScrollStitchOptions disabledOptions = enabledOptions;
    disabledOptions.autoIgnoreBottomEdge = false;

    ScrollStitchEngine disabledEngine(disabledOptions);
    QVERIFY(disabledEngine.start(frameWithUnstableFooter(0, 1)));
    const StitchFrameResult disabledR1 = disabledEngine.append(frameWithUnstableFooter(0, 2));
    const StitchFrameResult disabledR2 = disabledEngine.append(frameWithUnstableFooter(0, 3));
    QCOMPARE(disabledR1.bottomIgnorePx, 0);
    QCOMPARE(disabledR2.bottomIgnorePx, 0);
}

void tst_ScrollStitchEngine::testRepeatedListLikeFramesAccumulateHeight()
{
    const QImage content = makeContentImage(360, 5200, 27);

    ScrollStitchOptions options;
    options.goodThreshold = 0.84;
    options.partialThreshold = 0.68;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 260, 16, 0, 20)));

    int acceptedSegments = 0;
    for (int i = 1; i <= 10; ++i) {
        const StitchFrameResult result = engine.append(makeFrame(content, i * 38, 260, 16, 0, 20));
        if (result.quality == StitchQuality::Good || result.quality == StitchQuality::PartiallyGood) {
            ++acceptedSegments;
        }
    }

    const QImage composed = engine.composeFinal();
    QVERIFY(!composed.isNull());
    QVERIFY(acceptedSegments >= 6);
    QVERIFY(composed.height() > 520);
}

void tst_ScrollStitchEngine::testLargeFalseAppendGetsRejected()
{
    const QImage content = makeContentImage(360, 5200, 31);

    ScrollStitchOptions options;
    options.goodThreshold = 0.84;
    options.partialThreshold = 0.68;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 260, 16, 0, 20)));
    QVERIFY(engine.append(makeFrame(content, 40, 260, 16, 0, 20)).quality != StitchQuality::Bad);

    const int beforeHeight = engine.totalHeight();
    const StitchFrameResult suspicious = engine.append(makeFrame(content, 260, 260, 16, 0, 20));
    QVERIFY(suspicious.quality == StitchQuality::NoChange || suspicious.quality == StitchQuality::Bad);
    QVERIFY(suspicious.rejectionCode == StitchRejectionCode::ImplausibleAppend ||
            suspicious.rejectionCode == StitchRejectionCode::DuplicateTail ||
            suspicious.rejectionCode == StitchRejectionCode::LowConfidence);
    QCOMPARE(engine.totalHeight(), beforeHeight);
}

void tst_ScrollStitchEngine::testSeamBlenderFindsLowEnergyRow()
{
    QImage previous(320, 96, QImage::Format_ARGB32_Premultiplied);
    QImage current(320, 96, QImage::Format_ARGB32_Premultiplied);
    previous.fill(Qt::white);
    current.fill(Qt::white);

    QPainter p1(&previous);
    p1.fillRect(0, 48, previous.width(), 2, Qt::black);
    p1.end();

    QPainter p2(&current);
    p2.fillRect(0, 48, current.width(), 2, Qt::black);
    p2.end();

    ScrollSeamBlender blender;
    const int seamRow = blender.findMinimumEnergySeamRow(previous, current);

    QVERIFY(seamRow >= 0);
    QVERIFY(seamRow < previous.height());
    QVERIFY(qAbs(seamRow - 49) > 4);
}

void tst_ScrollStitchEngine::testMaxOutputHeightLimitIsEnforced()
{
    const QImage content = makeContentImage(320, 5000, 9);

    ScrollStitchOptions options;
    options.maxOutputPixels = 320 * 10000;
    options.maxOutputHeightPx = 300;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const StitchFrameResult r1 = engine.append(makeFrame(content, 40, 240));
    QVERIFY(r1.quality == StitchQuality::Good ||
            r1.quality == StitchQuality::PartiallyGood ||
            r1.quality == StitchQuality::NoChange);

    const QImage composed = engine.composeFinal();
    QVERIFY(!composed.isNull());
    QCOMPARE(composed.height(), 300);
}

void tst_ScrollStitchEngine::testUndoRestoresPreviousHeight()
{
    const QImage content = makeContentImage(320, 3200, 11);

    ScrollStitchEngine engine;
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    QVERIFY(engine.append(makeFrame(content, 40, 240)).quality != StitchQuality::Bad);
    const int heightAfterFirstAppend = engine.composeFinal().height();

    QVERIFY(engine.append(makeFrame(content, 80, 240)).quality != StitchQuality::Bad);
    const int heightAfterSecondAppend = engine.composeFinal().height();
    QVERIFY(heightAfterSecondAppend > heightAfterFirstAppend);
    QVERIFY(engine.canUndoLastSegment());

    QVERIFY(engine.undoLastSegment());
    QCOMPARE(engine.composeFinal().height(), heightAfterFirstAppend);
    QCOMPARE(engine.totalHeight(), heightAfterFirstAppend);
}

void tst_ScrollStitchEngine::testComposeFinalIsOpaque()
{
    const QImage content = makeContentImage(320, 2600, 13);

    ScrollStitchEngine engine;
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    QVERIFY(engine.append(makeFrame(content, 40, 240)).quality != StitchQuality::Bad);
    QVERIFY(engine.append(makeFrame(content, 80, 240)).quality != StitchQuality::Bad);

    const QImage finalImage = engine.composeFinal();
    QVERIFY(!finalImage.isNull());
    for (int y = 0; y < finalImage.height(); y += 17) {
        for (int x = 0; x < finalImage.width(); x += 19) {
            QCOMPARE(finalImage.pixelColor(x, y).alpha(), 255);
        }
    }
}

QTEST_MAIN(tst_ScrollStitchEngine)
#include "tst_ScrollStitchEngine.moc"
