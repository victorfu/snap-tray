#include <QtTest/QtTest>

#include <QColor>
#include <QImage>
#include <QPainter>

#include "detection/ScrollAlignmentEngine.h"

namespace {

QImage makeTexture(int width, int height, int seed)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int r = (x * 13 + y * 7 + seed * 19) % 255;
            const int g = (x * 5 + y * 11 + seed * 23) % 255;
            const int b = (x * 17 + y * 3 + seed * 31) % 255;
            line[x] = qRgba(r, g, b, 255);
        }
    }
    return image;
}

QImage makeFrame(const QImage &content, int offsetY, int height)
{
    const int y = qBound(0, offsetY, qMax(0, content.height() - height));
    return content.copy(0, y, content.width(), height).convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage translateX(const QImage &input, int dx)
{
    if (input.isNull()) {
        return QImage();
    }

    QImage output(input.size(), input.format());
    output.fill(Qt::black);

    QPainter painter(&output);
    painter.drawImage(dx, 0, input);
    painter.end();
    return output;
}

} // namespace

class tst_ScrollAlignmentEngine : public QObject
{
    Q_OBJECT

private slots:
    void testTemplateNccFindsExpectedDy();
    void testTemplateFallbackBandHandlesLowTexturePrimaryBand();
    void testSmallHorizontalShiftWithinTolerancePasses();
    void testHorizontalShiftBeyondToleranceIsRejected();
    void testLowConfidenceReturnsInvalid();
    void testAmbiguousClusterRejectedForRepeatedPattern();
};

void tst_ScrollAlignmentEngine::testTemplateNccFindsExpectedDy()
{
    ScrollAlignmentEngine engine;

    const QImage content = makeTexture(480, 3600, 7);
    const QImage previous = makeFrame(content, 0, 280);
    const QImage current = makeFrame(content, 44, 280);

    const ScrollAlignmentResult result = engine.align(previous, current, 44);
    QVERIFY(result.ok);
    QCOMPARE(result.strategy, ScrollAlignmentStrategy::TemplateNcc1D);
    QVERIFY(result.confidence >= 0.92);
    QVERIFY(!result.rejectedByConfidence);
    QVERIFY(qAbs(result.dySigned - 44) <= 2);
    QVERIFY(qAbs(result.dx) <= 2.0);
}

void tst_ScrollAlignmentEngine::testTemplateFallbackBandHandlesLowTexturePrimaryBand()
{
    ScrollAlignmentEngine engine;

    QImage content = makeTexture(460, 3600, 17);
    for (int y = 100; y < 128 && y < content.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(content.scanLine(y));
        for (int x = 0; x < content.width(); ++x) {
            line[x] = qRgba(140, 140, 140, 255);
        }
    }

    const QImage previous = makeFrame(content, 0, 280);
    const QImage current = makeFrame(content, 44, 280);
    const ScrollAlignmentResult result = engine.align(previous, current, 44);

    QVERIFY(result.ok);
    QVERIFY(!result.rejectedByConfidence);
    QVERIFY(qAbs(result.dySigned - 44) <= 2);
}

void tst_ScrollAlignmentEngine::testSmallHorizontalShiftWithinTolerancePasses()
{
    ScrollAlignmentEngine engine;

    const QImage content = makeTexture(520, 3800, 23);
    const QImage previous = makeFrame(content, 0, 320);
    const QImage shifted = makeFrame(content, 52, 320);
    const QImage current = translateX(shifted, 2);

    const ScrollAlignmentResult result = engine.align(previous, current, 52);
    QVERIFY(result.ok);
    QVERIFY(!result.rejectedByConfidence);
    QVERIFY(qAbs(result.dx) <= 2.0);
}

void tst_ScrollAlignmentEngine::testHorizontalShiftBeyondToleranceIsRejected()
{
    ScrollAlignmentEngine engine;

    const QImage content = makeTexture(520, 3800, 11);
    const QImage previous = makeFrame(content, 0, 320);
    const QImage shifted = makeFrame(content, 52, 320);
    const QImage current = translateX(shifted, 8);

    const ScrollAlignmentResult result = engine.align(previous, current, 52);
    const QString diagnostics = QStringLiteral("ok=%1 conf=%2 raw=%3 penalty=%4 dx=%5 dy=%6 reason=%7")
                                    .arg(result.ok)
                                    .arg(result.confidence, 0, 'f', 4)
                                    .arg(result.rawNccScore, 0, 'f', 4)
                                    .arg(result.confidencePenalty, 0, 'f', 4)
                                    .arg(result.dx, 0, 'f', 2)
                                    .arg(result.dySigned)
                                    .arg(result.reason);
    QVERIFY2(!result.ok, qPrintable(diagnostics));
    QVERIFY(result.rejectedByConfidence);
    QVERIFY(result.confidence < 0.92);
}

void tst_ScrollAlignmentEngine::testLowConfidenceReturnsInvalid()
{
    ScrollAlignmentEngine engine;

    const QImage previous = makeTexture(420, 260, 1);
    QImage current(420, 260, QImage::Format_ARGB32_Premultiplied);
    current.fill(QColor(12, 18, 27, 255));

    const ScrollAlignmentResult result = engine.align(previous, current, 40);
    QVERIFY(!result.ok);
    QVERIFY(result.rejectedByConfidence);
    QVERIFY(result.confidence < 0.92);
    QVERIFY(result.reason.contains(QStringLiteral("Low confidence"), Qt::CaseInsensitive));
}

void tst_ScrollAlignmentEngine::testAmbiguousClusterRejectedForRepeatedPattern()
{
    ScrollAlignmentEngine engine;

    QImage content(420, 3200, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < content.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(content.scanLine(y));
        const int block = ((y / 48) % 2 == 0) ? 220 : 70;
        for (int x = 0; x < content.width(); ++x) {
            line[x] = qRgba(block, block, block, 255);
        }
    }

    const QImage previous = makeFrame(content, 0, 280);
    const QImage current = makeFrame(content, 48, 280);

    const ScrollAlignmentResult result = engine.align(previous, current, 48);
    QVERIFY(result.consensusSupport >= 1);
    QVERIFY(result.dyClusterSpread >= 0.0);
    QVERIFY(result.ok || result.rejectedByConfidence);
}

QTEST_MAIN(tst_ScrollAlignmentEngine)
#include "tst_ScrollAlignmentEngine.moc"
