#include <QtTest>
#include <QPainter>
#include <QFontDatabase>
#include <memory>
#include "OCRManager.h"
#include "detection/CredentialDetector.h"

class TestOCRCredentialGeometry : public QObject
{
    Q_OBJECT
private slots:
    void nativeOcrCoverage_data();
    void nativeOcrCoverage();
};

void TestOCRCredentialGeometry::nativeOcrCoverage_data()
{
    QTest::addColumn<QString>("family");
    QTest::addColumn<int>("pixelSize");
    for (const auto& family : {QStringLiteral("Arial"),
                              QFontDatabase::systemFont(QFontDatabase::FixedFont).family()}) {
        for (int size : {20, 32, 48}) {
            QTest::addRow("%s-%d", qPrintable(family), size) << family << size;
        }
    }
}

void TestOCRCredentialGeometry::nativeOcrCoverage()
{
    if (!qEnvironmentVariableIsSet("SNAPTRAY_TEST_NATIVE_OCR")) {
        QSKIP("Set SNAPTRAY_TEST_NATIVE_OCR=1 on a host with native OCR to run this integration fixture");
    }
    QFETCH(QString, family);
    QFETCH(int, pixelSize);
    QFont font(family);
    font.setPixelSize(pixelSize);
    const QString prefix = QStringLiteral("password: ");
    // A mixed suffix disambiguates Vision's automatic orientation detection.
    const QString value = QString(16, QLatin1Char('W')) + QStringLiteral("aB12");
    const QPointF baseline(20, 70);
    QImage input(1200, 160, QImage::Format_RGB32);
    input.fill(Qt::white);
    { QPainter p(&input); p.setFont(font); p.setPen(Qt::black); p.drawText(baseline, prefix + value); }
    const qreal valueStart = baseline.x() + QFontMetricsF(font).horizontalAdvance(prefix);
    OCRManager manager;
    struct RecognitionState {
        bool complete = false;
        OCRResult result;
    };
    const auto state = std::make_shared<RecognitionState>();
    manager.recognizeText(QPixmap::fromImage(input), [state](const OCRResult& recognized) {
        state->result = recognized;
        state->complete = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(state->complete, 60000);
    const auto& result = state->result;
    QVERIFY2(result.success, qPrintable(result.error));
    // OCR may confuse repeated W/w glyphs; this checks geometry for the
    // recognized credential, not exact transcription of the synthetic value.
    QVERIFY2(result.text.startsWith(prefix, Qt::CaseInsensitive), qPrintable(result.text));
    QVERIFY(result.text.size() > prefix.size());
    QVERIFY(!result.blocks.isEmpty());
#ifdef Q_OS_MACOS
    bool hasGeometry = false;
    for (const auto& block : result.blocks) {
        hasGeometry |= !block.textRanges.isEmpty();
    }
    QVERIFY(hasGeometry);
#endif
    const auto regions = CredentialDetector::detect(result.blocks, input.size());
    QVERIFY(!regions.isEmpty());
    // Independent raster oracle: all visibly dark value pixels must be covered.
    // No OCR geometry or character-width ratios are used for expected bounds.
    for (int y = 0; y < input.height(); ++y) {
        for (int x = qCeil(valueStart); x < input.width(); ++x) {
            if (qGray(input.pixel(x, y)) >= 128) continue;
            bool covered = false;
            for (const auto& rect : regions) covered |= rect.contains(x, y);
            QVERIFY2(covered, qPrintable(QStringLiteral("Uncovered value pixel (%1,%2)").arg(x).arg(y)));
        }
    }
}

QTEST_MAIN(TestOCRCredentialGeometry)
#include "tst_OCRCredentialGeometry.moc"
