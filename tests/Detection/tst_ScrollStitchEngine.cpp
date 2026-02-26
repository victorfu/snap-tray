#include <QtTest/QtTest>
#include <QPainter>
#include <QPainterPath>
#include <cstring>

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
                 int footerNoiseSeed = -1)
{
    const int copyY = qBound(0, offsetY, qMax(0, content.height() - frameHeight));
    QImage frame = content.copy(0, copyY, content.width(), frameHeight).convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (stickyHeaderPx > 0) {
        QPainter painter(&frame);
        painter.fillRect(0, 0, frame.width(), stickyHeaderPx, QColor(31, 41, 55, 255));
    }

    if (stickyFooterPx > 0) {
        QPainter painter(&frame);
        painter.fillRect(0, frame.height() - stickyFooterPx, frame.width(), stickyFooterPx, QColor(15, 23, 42, 255));
    }

    if (footerNoiseSeed >= 0) {
        const int noiseRows = qMin(28, frame.height());
        for (int row = frame.height() - noiseRows; row < frame.height(); ++row) {
            QRgb *line = reinterpret_cast<QRgb *>(frame.scanLine(row));
            for (int x = 0; x < frame.width(); ++x) {
                const int value = (row * 19 + x * 37 + footerNoiseSeed * 53) % 255;
                line[x] = qRgba(value, (value * 7) % 255, (value * 13) % 255, 255);
            }
        }
    }

    return frame;
}

QImage addMildNoise(const QImage &input)
{
    QImage noisy = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < noisy.height(); y += 6) {
        QRgb *line = reinterpret_cast<QRgb *>(noisy.scanLine(y));
        for (int x = (y % 5); x < noisy.width(); x += 17) {
            const int r = qBound(0, qRed(line[x]) + 9, 255);
            const int g = qBound(0, qGreen(line[x]) - 7, 255);
            const int b = qBound(0, qBlue(line[x]) + 5, 255);
            line[x] = qRgba(r, g, b, 255);
        }
    }
    return noisy;
}

QImage roundedFrame(const QImage &input, int radius)
{
    QImage rounded(input.size(), QImage::Format_ARGB32);
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, input.width(), input.height()), radius, radius);
    painter.setClipPath(clipPath);
    painter.drawImage(0, 0, input.convertToFormat(QImage::Format_ARGB32));
    painter.end();

    return rounded;
}

} // namespace

class tst_ScrollStitchEngine : public QObject
{
    Q_OBJECT

private slots:
    void testGoodAppend();
    void testPartiallyGoodAppend();
    void testBadAppend();
    void testStickyHeaderFooterDetection();
    void testAutoIgnoreBottomEdge();
    void testAutoIgnoreBottomEdgeCommitsTrim();
    void testStableBottomEdgeDoesNotTrim();
    void testNoChangeFrame();
    void testNoChangeFrameDoesNotTrimCommittedContent();
    void testColumnSampleFallbackHandlesStaticSideGutters();
    void testBestGuessNotUsedForLowActivityFrames();
    void testStrategyIsExposedInFrameResult();
    void testLowActivityDoesNotUseBestGuess();
    void testUndoLastSegmentRestoresPreviousHeight();
    void testPreviewMetaIncludesLastStrategyAndConfidence();
    void testPreviewDetailAndMinimapLayout();
    void testPreviewCacheDoesNotAffectStitchOutput();
    void testPreviewCacheInvalidatesAfterAppend();
    void testRoundedAlphaFrameIsSquaredBeforeStitch();
    void testComposeFinalIsOpaque();
    void testAbnormalLargeDyRejected();
    void testStartClampsFirstFrameToMaxOutputPixels();
};

void tst_ScrollStitchEngine::testGoodAppend()
{
    const QImage content = makeContentImage(320, 2200, 1);

    ScrollStitchOptions options;
    options.goodThreshold = 0.75;
    options.partialThreshold = 0.60;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const StitchFrameResult result = engine.append(makeFrame(content, 48, 240));
    QCOMPARE(result.quality, StitchQuality::Good);
    QVERIFY(result.appendHeight > 0);
    QVERIFY(engine.composeFinal().height() > 240);
}

void tst_ScrollStitchEngine::testPartiallyGoodAppend()
{
    const QImage content = makeContentImage(320, 2200, 2);

    ScrollStitchOptions options;
    options.goodThreshold = 0.999999;
    options.partialThreshold = 0.60;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const QImage frame = addMildNoise(makeFrame(content, 44, 240));
    const StitchFrameResult result = engine.append(frame);

    QCOMPARE(result.quality, StitchQuality::PartiallyGood);
    QVERIFY(result.appendHeight > 0);
}

void tst_ScrollStitchEngine::testBadAppend()
{
    const QImage contentA = makeContentImage(320, 2200, 3);

    ScrollStitchOptions options;
    options.goodThreshold = 0.999999;
    options.partialThreshold = 0.999995;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(contentA, 0, 220)));

    QImage unrelated(320, 220, QImage::Format_ARGB32_Premultiplied);
    unrelated.fill(QColor(17, 22, 31, 255));
    const StitchFrameResult result = engine.append(unrelated);
    QCOMPARE(result.quality, StitchQuality::Bad);
}

void tst_ScrollStitchEngine::testStickyHeaderFooterDetection()
{
    const QImage content = makeContentImage(320, 2600, 4);

    ScrollStitchOptions options;
    options.goodThreshold = 0.70;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240, 18, 14)));

    const StitchFrameResult r1 = engine.append(makeFrame(content, 40, 240, 18, 14));
    const StitchFrameResult r2 = engine.append(makeFrame(content, 80, 240, 18, 14));
    const StitchFrameResult r3 = engine.append(makeFrame(content, 120, 240, 18, 14));

    QVERIFY(r1.quality != StitchQuality::Bad);
    QVERIFY(r2.quality != StitchQuality::Bad);
    QVERIFY(r3.quality != StitchQuality::Bad);
    QVERIFY(r3.stickyHeaderPx > 0);
    QVERIFY(r3.stickyFooterPx > 0);

    const QImage composed = engine.composeFinal();
    QVERIFY(!composed.isNull());
    QVERIFY(composed.height() < 240 * 4);
}

void tst_ScrollStitchEngine::testAutoIgnoreBottomEdge()
{
    const QImage content = makeContentImage(320, 2500, 5);

    ScrollStitchOptions ignoreOptions;
    ignoreOptions.goodThreshold = 0.75;
    ignoreOptions.partialThreshold = 0.55;
    ignoreOptions.minScrollAmount = 8;
    ignoreOptions.autoIgnoreBottomEdge = true;

    ScrollStitchOptions strictOptions = ignoreOptions;
    strictOptions.autoIgnoreBottomEdge = false;

    const QImage first = makeFrame(content, 0, 220, 0, 0, 1);
    const QImage next = makeFrame(content, 42, 220, 0, 0, 2);

    ScrollStitchEngine ignoreEngine(ignoreOptions);
    ScrollStitchEngine strictEngine(strictOptions);

    QVERIFY(ignoreEngine.start(first));
    QVERIFY(strictEngine.start(first));

    const StitchFrameResult ignoreResult = ignoreEngine.append(next);
    const StitchFrameResult strictResult = strictEngine.append(next);

    QVERIFY(ignoreResult.quality != StitchQuality::Bad);
    QVERIFY(ignoreResult.confidence >= strictResult.confidence);
}

void tst_ScrollStitchEngine::testAutoIgnoreBottomEdgeCommitsTrim()
{
    const QImage content = makeContentImage(320, 3600, 21);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;
    options.autoIgnoreBottomEdge = true;

    ScrollStitchOptions strictOptions = options;
    strictOptions.autoIgnoreBottomEdge = false;

    const QVector<QImage> frames{
        makeFrame(content, 0, 240, 0, 0, 1),
        makeFrame(content, 40, 240, 0, 0, 2),
        makeFrame(content, 80, 240, 0, 0, 3),
        makeFrame(content, 120, 240, 0, 0, 4),
        makeFrame(content, 160, 240, 0, 0, 5),
    };

    ScrollStitchEngine ignoreEngine(options);
    ScrollStitchEngine strictEngine(strictOptions);
    QVERIFY(ignoreEngine.start(frames.first()));
    QVERIFY(strictEngine.start(frames.first()));

    int naiveAccumulatedHeight = frames.first().height();
    for (int i = 1; i < frames.size(); ++i) {
        const StitchFrameResult ignoreResult = ignoreEngine.append(frames.at(i));
        const StitchFrameResult strictResult = strictEngine.append(frames.at(i));
        QVERIFY(ignoreResult.quality != StitchQuality::Bad);
        QVERIFY(strictResult.quality != StitchQuality::Bad);
        naiveAccumulatedHeight += ignoreResult.appendHeight;
    }

    // Footer is unstable noise instead of a stable sticky band.
    QCOMPARE(ignoreEngine.stickyFooterPx(), 0);

    const int ignoreHeight = ignoreEngine.composeFinal().height();
    const int strictHeight = strictEngine.composeFinal().height();

    // Commit-time trim must actually reduce previously committed rows after
    // bottom-ignore stabilizes; without trim this stays at naive height.
    QVERIFY(ignoreHeight < naiveAccumulatedHeight);
    QVERIFY(ignoreHeight < strictHeight);
}

void tst_ScrollStitchEngine::testStableBottomEdgeDoesNotTrim()
{
    const QImage content = makeContentImage(320, 3200, 13);

    ScrollStitchOptions ignoreOptions;
    ignoreOptions.goodThreshold = 0.75;
    ignoreOptions.partialThreshold = 0.55;
    ignoreOptions.minScrollAmount = 8;
    ignoreOptions.autoIgnoreBottomEdge = true;

    ScrollStitchOptions strictOptions = ignoreOptions;
    strictOptions.autoIgnoreBottomEdge = false;

    const QImage frame0 = makeFrame(content, 0, 240, 0, 14);
    const QImage frame1 = makeFrame(content, 40, 240, 0, 14);
    const QImage frame2 = makeFrame(content, 80, 240, 0, 14);
    const QImage frame3 = makeFrame(content, 120, 240, 0, 14);
    const QImage frame4 = makeFrame(content, 160, 240, 0, 14);

    ScrollStitchEngine ignoreEngine(ignoreOptions);
    ScrollStitchEngine strictEngine(strictOptions);
    QVERIFY(ignoreEngine.start(frame0));
    QVERIFY(strictEngine.start(frame0));

    const QVector<QImage> frames{frame1, frame2, frame3, frame4};
    for (const QImage &frame : frames) {
        const StitchFrameResult ignoreResult = ignoreEngine.append(frame);
        const StitchFrameResult strictResult = strictEngine.append(frame);
        QVERIFY(ignoreResult.quality != StitchQuality::Bad);
        QVERIFY(strictResult.quality != StitchQuality::Bad);
    }

    QVERIFY(ignoreEngine.stickyFooterPx() > 0);
    QVERIFY(strictEngine.stickyFooterPx() > 0);
    QCOMPARE(ignoreEngine.composeFinal().height(), strictEngine.composeFinal().height());
}

void tst_ScrollStitchEngine::testNoChangeFrame()
{
    const QImage content = makeContentImage(320, 1800, 6);
    const QImage frame = makeFrame(content, 0, 240);

    ScrollStitchEngine engine;
    QVERIFY(engine.start(frame));

    const StitchFrameResult result = engine.append(frame);
    QCOMPARE(result.quality, StitchQuality::NoChange);
}

void tst_ScrollStitchEngine::testNoChangeFrameDoesNotTrimCommittedContent()
{
    const QImage content = makeContentImage(320, 2600, 7);

    ScrollStitchOptions options;
    options.goodThreshold = 0.70;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 12;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240, 10, 8)));

    const StitchFrameResult r1 = engine.append(makeFrame(content, 40, 240, 10, 8));
    const StitchFrameResult r2 = engine.append(makeFrame(content, 80, 240, 10, 8));
    const StitchFrameResult r3 = engine.append(makeFrame(content, 120, 240, 10, 8));
    QVERIFY(r1.quality != StitchQuality::Bad);
    QVERIFY(r2.quality != StitchQuality::Bad);
    QVERIFY(r3.quality != StitchQuality::Bad);
    QVERIFY(engine.stickyFooterPx() > 0);

    const int heightBefore = engine.composeFinal().height();
    const StitchFrameResult noChange = engine.append(makeFrame(content, 132, 240, 10, 8));
    QCOMPARE(noChange.quality, StitchQuality::NoChange);
    QCOMPARE(engine.composeFinal().height(), heightBefore);
}

void tst_ScrollStitchEngine::testColumnSampleFallbackHandlesStaticSideGutters()
{
    const QImage content = makeContentImage(320, 3200, 31);

    ScrollStitchOptions options;
    options.goodThreshold = 0.82;
    options.partialThreshold = 0.62;
    options.minScrollAmount = 8;

    auto applyGuttersAndColumnNoise = [](QImage &image, int noiseSeed) {
        QPainter painter(&image);
        painter.fillRect(0, 0, 56, image.height(), QColor(17, 24, 39, 255));
        painter.fillRect(image.width() - 56, 0, 56, image.height(), QColor(17, 24, 39, 255));
        painter.end();

        const QVector<int> noisyColumns{92, 118, 146, 172, 208};
        for (const int startX : noisyColumns) {
            const int colWidth = qMin(8, image.width() - startX);
            if (colWidth <= 0) {
                continue;
            }
            for (int y = 0; y < image.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < colWidth; ++x) {
                    const int px = startX + x;
                    const int value = (px * 11 + y * 19 + noiseSeed * 41) % 255;
                    line[px] = qRgba(value, (value * 3) % 255, (value * 7) % 255, 255);
                }
            }
        }
    };

    QImage first = makeFrame(content, 0, 240);
    QImage next = makeFrame(content, 44, 240);
    applyGuttersAndColumnNoise(first, 1);
    applyGuttersAndColumnNoise(next, 2);

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(first));
    const StitchFrameResult result = engine.append(next);

    QVERIFY(result.quality != StitchQuality::Bad);
    QCOMPARE(result.strategy, StitchMatchStrategy::ColumnSample);
}

void tst_ScrollStitchEngine::testBestGuessNotUsedForLowActivityFrames()
{
    const QImage content = makeContentImage(320, 2600, 8);

    ScrollStitchOptions options;
    options.goodThreshold = 0.70;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const StitchFrameResult establish = engine.append(makeFrame(content, 40, 240));
    QVERIFY(establish.quality != StitchQuality::Bad);

    QImage lowActivityFrame = makeFrame(content, 40, 240);
    for (int y = lowActivityFrame.height() - 20; y < lowActivityFrame.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(lowActivityFrame.scanLine(y));
        for (int x = 0; x < lowActivityFrame.width(); ++x) {
            if ((x + y) % 9 == 0) {
                line[x] = qRgba(255, 255, 255, 255);
            }
        }
    }

    const StitchFrameResult result = engine.append(lowActivityFrame);
    QCOMPARE(result.quality, StitchQuality::NoChange);
    QVERIFY(result.strategy != StitchMatchStrategy::BestGuess);
}

void tst_ScrollStitchEngine::testStrategyIsExposedInFrameResult()
{
    const QImage content = makeContentImage(320, 2400, 32);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const StitchFrameResult result = engine.append(makeFrame(content, 40, 240));
    QVERIFY(result.quality != StitchQuality::Bad);
    QVERIFY(result.strategy == StitchMatchStrategy::RowDiff ||
            result.strategy == StitchMatchStrategy::ColumnSample);
}

void tst_ScrollStitchEngine::testLowActivityDoesNotUseBestGuess()
{
    testBestGuessNotUsedForLowActivityFrames();
}

void tst_ScrollStitchEngine::testUndoLastSegmentRestoresPreviousHeight()
{
    const QImage content = makeContentImage(320, 3200, 37);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
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
    QCOMPARE(engine.frameCount(), 2);
}

void tst_ScrollStitchEngine::testPreviewMetaIncludesLastStrategyAndConfidence()
{
    const QImage content = makeContentImage(320, 2600, 38);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    const StitchFrameResult appendResult = engine.append(makeFrame(content, 40, 240));
    QVERIFY(appendResult.quality != StitchQuality::Bad);

    const StitchPreviewMeta meta = engine.previewMeta();
    QVERIFY(meta.hasLastAppend);
    QCOMPARE(meta.frames, engine.frameCount());
    QCOMPARE(meta.height, engine.totalHeight());
    QCOMPARE(meta.lastAppend, appendResult.appendHeight);
    QVERIFY(meta.lastConfidence > 0.0);
    QVERIFY(meta.lastStrategy == StitchMatchStrategy::RowDiff ||
            meta.lastStrategy == StitchMatchStrategy::ColumnSample ||
            meta.lastStrategy == StitchMatchStrategy::BestGuess);
    QCOMPARE(meta.stickyHeaderPx, engine.stickyHeaderPx());
    QCOMPARE(meta.stickyFooterPx, engine.stickyFooterPx());
}

void tst_ScrollStitchEngine::testPreviewDetailAndMinimapLayout()
{
    const QImage content = makeContentImage(320, 4800, 9);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 260)));
    QVERIFY(engine.append(makeFrame(content, 60, 260)).quality != StitchQuality::Bad);
    QVERIFY(engine.append(makeFrame(content, 120, 260)).quality != StitchQuality::Bad);
    QVERIFY(engine.append(makeFrame(content, 180, 260)).quality != StitchQuality::Bad);

    const QImage preview = engine.preview(560, 360);
    QVERIFY(!preview.isNull());
    QCOMPARE(preview.size(), QSize(560, 360));

    bool hasViewportIndicator = false;
    for (int y = 0; y < preview.height(); ++y) {
        const QColor c = preview.pixelColor(preview.width() - 40, y);
        if (c.red() > 220 && c.green() > 170 && c.blue() < 130) {
            hasViewportIndicator = true;
            break;
        }
    }
    QVERIFY(hasViewportIndicator);
}

void tst_ScrollStitchEngine::testPreviewCacheDoesNotAffectStitchOutput()
{
    const QImage content = makeContentImage(320, 4600, 41);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    const QVector<QImage> frames{
        makeFrame(content, 0, 240),
        makeFrame(content, 40, 240),
        makeFrame(content, 80, 240),
        makeFrame(content, 120, 240),
        makeFrame(content, 160, 240)
    };

    ScrollStitchEngine baselineEngine(options);
    ScrollStitchEngine previewEngine(options);
    QVERIFY(baselineEngine.start(frames.first()));
    QVERIFY(previewEngine.start(frames.first()));

    for (int i = 1; i < frames.size(); ++i) {
        const StitchFrameResult baselineResult = baselineEngine.append(frames.at(i));
        QVERIFY(baselineResult.quality != StitchQuality::Bad);

        const StitchFrameResult previewResult = previewEngine.append(frames.at(i));
        QVERIFY(previewResult.quality != StitchQuality::Bad);

        // Preview requests should not mutate stitch state or output.
        for (int j = 0; j < 6; ++j) {
            const QImage preview = previewEngine.preview(560, 360);
            QVERIFY(!preview.isNull());
        }
    }

    const QImage baseline = baselineEngine.composeFinal();
    const QImage withPreview = previewEngine.composeFinal();
    QVERIFY(!baseline.isNull());
    QVERIFY(!withPreview.isNull());
    QCOMPARE(withPreview, baseline);
}

void tst_ScrollStitchEngine::testPreviewCacheInvalidatesAfterAppend()
{
    const QImage content = makeContentImage(320, 3200, 42);

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    QVERIFY(engine.append(makeFrame(content, 40, 240)).quality != StitchQuality::Bad);

    const QImage previewBefore = engine.preview(560, 360);
    const QImage previewBeforeSecond = engine.preview(560, 360);
    QVERIFY(!previewBefore.isNull());
    QVERIFY(!previewBeforeSecond.isNull());
    QCOMPARE(previewBefore.cacheKey(), previewBeforeSecond.cacheKey());

    QVERIFY(engine.append(makeFrame(content, 80, 240)).quality != StitchQuality::Bad);

    const QImage previewAfter = engine.preview(560, 360);
    QVERIFY(!previewAfter.isNull());
    QVERIFY(previewAfter.cacheKey() != previewBefore.cacheKey());
}

void tst_ScrollStitchEngine::testRoundedAlphaFrameIsSquaredBeforeStitch()
{
    const QImage content = makeContentImage(320, 2200, 10);

    ScrollStitchOptions options;
    options.goodThreshold = 0.70;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    const QImage raw0 = makeFrame(content, 0, 240);
    const QImage raw1 = makeFrame(content, 40, 240);
    const QImage rounded0 = roundedFrame(raw0, 26);
    const QImage rounded1 = roundedFrame(raw1, 26);

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(rounded0));
    const StitchFrameResult result = engine.append(rounded1);
    QVERIFY(result.quality != StitchQuality::Bad);

    const QImage finalImage = engine.composeFinal();
    QVERIFY(!finalImage.isNull());
    QVERIFY(finalImage.pixelColor(0, 0).alpha() == 255);
    QVERIFY(finalImage.pixelColor(finalImage.width() - 1, 0).alpha() == 255);
    QVERIFY(finalImage.pixelColor(0, finalImage.height() - 1).alpha() == 255);
    QVERIFY(finalImage.pixelColor(finalImage.width() - 1, finalImage.height() - 1).alpha() == 255);
}

void tst_ScrollStitchEngine::testComposeFinalIsOpaque()
{
    const QImage content = makeContentImage(320, 2600, 11);

    ScrollStitchEngine engine;
    QVERIFY(engine.start(roundedFrame(makeFrame(content, 0, 240), 28)));
    QVERIFY(engine.append(roundedFrame(makeFrame(content, 40, 240), 28)).quality != StitchQuality::Bad);
    QVERIFY(engine.append(roundedFrame(makeFrame(content, 80, 240), 28)).quality != StitchQuality::Bad);

    const QImage finalImage = engine.composeFinal();
    QVERIFY(!finalImage.isNull());
    for (int y = 0; y < finalImage.height(); y += 17) {
        for (int x = 0; x < finalImage.width(); x += 19) {
            QCOMPARE(finalImage.pixelColor(x, y).alpha(), 255);
        }
    }
}

void tst_ScrollStitchEngine::testAbnormalLargeDyRejected()
{
    QImage content(320, 4200, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < content.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(content.scanLine(y));
        quint32 state = static_cast<quint32>(0x9E3779B9u * static_cast<quint32>(y + 1));
        for (int x = 0; x < content.width(); ++x) {
            state = (state * 1664525u) + 1013904223u;
            const int r = static_cast<int>((state >> 16) & 0xFF);
            const int g = static_cast<int>((state >> 8) & 0xFF);
            const int b = static_cast<int>(state & 0xFF);
            line[x] = qRgba(r, g, b, 255);
        }
    }

    ScrollStitchOptions options;
    options.goodThreshold = 0.72;
    options.partialThreshold = 0.55;
    options.minScrollAmount = 8;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));
    const StitchFrameResult normal1 = engine.append(makeFrame(content, 30, 240));
    const QImage prevFrame = makeFrame(content, 60, 240);
    const StitchFrameResult normal2 = engine.append(prevFrame);
    QVERIFY(normal1.quality != StitchQuality::Bad);
    QVERIFY(normal2.quality != StitchQuality::Bad);

    QImage jumpFrame(320, 240, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < jumpFrame.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(jumpFrame.scanLine(y));
        if (y < 48) {
            const QRgb *src = reinterpret_cast<const QRgb *>(prevFrame.constScanLine(192 + y));
            memcpy(line, src, static_cast<size_t>(jumpFrame.bytesPerLine()));
            continue;
        }

        quint32 state = static_cast<quint32>(0x85EBCA6Bu * static_cast<quint32>(y + 1));
        for (int x = 0; x < jumpFrame.width(); ++x) {
            state = (state * 22695477u) + 1u;
            line[x] = qRgba(static_cast<int>((state >> 16) & 0xFF),
                            static_cast<int>((state >> 8) & 0xFF),
                            static_cast<int>(state & 0xFF),
                            255);
        }
    }

    const int heightBeforeJump = engine.composeFinal().height();
    const StitchFrameResult jump = engine.append(jumpFrame);
    QCOMPARE(jump.quality, StitchQuality::NoChange);
    QCOMPARE(engine.composeFinal().height(), heightBeforeJump);
}

void tst_ScrollStitchEngine::testStartClampsFirstFrameToMaxOutputPixels()
{
    const QImage content = makeContentImage(320, 2200, 12);

    ScrollStitchOptions options;
    options.maxOutputPixels = 320 * 120;

    ScrollStitchEngine engine(options);
    QVERIFY(engine.start(makeFrame(content, 0, 240)));

    const QImage composed = engine.composeFinal();
    QVERIFY(!composed.isNull());
    QCOMPARE(composed.width(), 320);
    QCOMPARE(composed.height(), 120);
    QCOMPARE(engine.totalHeight(), 120);
    QCOMPARE(engine.frameCount(), 1);
}

QTEST_MAIN(tst_ScrollStitchEngine)
#include "tst_ScrollStitchEngine.moc"
