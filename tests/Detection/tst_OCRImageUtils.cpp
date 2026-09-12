#include <QtTest>
#include "utils/OCRImageUtils.h"
#include "detection/CredentialDetector.h"

class TestOCRImageUtils : public QObject
{
    Q_OBJECT
private slots:
    void testPhysicalLimit_data();
    void testPhysicalLimit();
    void testCredentialCoordinatesAfterResize();
    void testInvalidInput();
};

void TestOCRImageUtils::testPhysicalLimit_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<qreal>("dpr");
    for (QSize size : {QSize(128, 128), QSize(129, 64), QSize(64, 129),
                       QSize(1024, 768), QSize(8192, 1), QSize(1, 8192)}) {
        for (qreal dpr : {1.0, 1.5, 2.0}) {
            QTest::addRow("%dx%d-dpr-%g", size.width(), size.height(), dpr) << size << dpr;
        }
    }
}

void TestOCRImageUtils::testPhysicalLimit()
{
    QFETCH(QSize, size);
    QFETCH(qreal, dpr);
    QImage original(size, QImage::Format_RGB32);
    original.fill(Qt::white);
    original.setPixel(0, 0, qRgb(255, 0, 0));
    original.setDevicePixelRatio(dpr);
    const QImage fitted = OCRImageUtils::fitForRecognition(original, 128);
    QVERIFY(!fitted.isNull());
    QVERIFY(fitted.width() <= 128 && fitted.height() <= 128);
    QCOMPARE(fitted.devicePixelRatio(), 1.0);
    QCOMPARE(original.size(), size);
    QCOMPARE(original.devicePixelRatio(), dpr);
    if (size.width() <= 128 && size.height() <= 128) {
        original.setDevicePixelRatio(1.0);
        QCOMPARE(fitted, original);
    }
    // A rectangle covering the prepared image must cover the whole source
    // again, including fractional DPR and rounded resized dimensions.
    QCOMPARE(OCRImageUtils::normalizedBoundingRect(QRectF(QPointF(), fitted.size()), fitted.size()),
             QRectF(0, 0, 1, 1));
}

void TestOCRImageUtils::testCredentialCoordinatesAfterResize()
{
    QImage original(7001, 5003, QImage::Format_RGB32);
    original.fill(Qt::white);
    original.setDevicePixelRatio(2.0);
    const QImage fitted = OCRImageUtils::fitForRecognition(original, 2048);
    const QRectF expected(0.25, 0.5, 0.25, 0.125);
    const QRectF recognized(expected.x() * fitted.width(), expected.y() * fitted.height(),
                            expected.width() * fitted.width(), expected.height() * fitted.height());
    OCRTextBlock block;
    block.text = QStringLiteral("sk-ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    block.boundingRect = OCRImageUtils::normalizedBoundingRect(recognized, fitted.size());
    QCOMPARE(block.boundingRect, expected);
    const auto regions = CredentialDetector::detect({block}, original.size());
    QCOMPARE(regions.size(), 1);
    QVERIFY(regions.first().contains(QPoint(qRound(0.375 * original.width()),
                                            qRound(0.5625 * original.height()))));
    QVERIFY(regions.first().width() >= qFloor(expected.width() * original.width()));
    QVERIFY(regions.first().height() >= qFloor(expected.height() * original.height()));
}

void TestOCRImageUtils::testInvalidInput()
{
    QVERIFY(OCRImageUtils::fitForRecognition({}, 128).isNull());
    QVERIFY(OCRImageUtils::fitForRecognition(QImage(10, 10, QImage::Format_RGB32), 0).isNull());
    QCOMPARE(OCRImageUtils::normalizedBoundingRect(QRectF(1, 2, 3, 4), {}), QRectF());
}

QTEST_MAIN(TestOCRImageUtils)
#include "tst_OCRImageUtils.moc"
