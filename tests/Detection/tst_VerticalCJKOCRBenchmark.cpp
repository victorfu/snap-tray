// Opt-in measurement of the vertical CJK retry against native OCR.
//
//   SNAPTRAY_TEST_NATIVE_OCR=1 ./Detection_VerticalCJKOCRBenchmark
//   SNAPTRAY_OCR_BENCHMARK_DIR=/some/dir  (optional: saves samples + results.tsv)
//
// It reports character error rate and time for native OCR on the source and
// on the re-laid image. It asserts nothing about gains: the numbers decide
// whether the retry is worth more than an explicit, off-by-default action.

#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <memory>
#include <vector>

#include "OCRManager.h"
#include "utils/VerticalCJKNormalizer.h"
#include "VerticalCJKSamples.h"

namespace {

int editDistance(const QString& a, const QString& b)
{
    std::vector<int> row(static_cast<size_t>(b.size()) + 1);
    for (int j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (int i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

QString withoutWhitespace(const QString& text)
{
    QString result;
    for (const QChar ch : text) {
        if (!ch.isSpace()) {
            result.append(ch);
        }
    }
    return result;
}

double characterErrorRate(const QString& recognized, const QString& expected)
{
    return static_cast<double>(editDistance(withoutWhitespace(recognized), expected)) / expected.size();
}

QString installedLanguage(const QString& prefix)
{
    for (const auto& language : OCRManager::availableLanguages()) {
        if (language.code.startsWith(prefix)) {
            return language.code;
        }
    }
    return {};
}

struct Timed {
    OCRResult result;
    qint64 ms = 0;
};

Timed recognize(OCRManager& manager, const QImage& image)
{
    auto result = std::make_shared<OCRResult>();
    auto done = std::make_shared<bool>(false);
    QElapsedTimer timer;
    timer.start();
    manager.recognizeText(QPixmap::fromImage(image), [result, done](const OCRResult& recognized) {
        *result = recognized;
        *done = true;
    });
    if (!QTest::qWaitFor([done]() { return *done; }, 60000)) {
        result->error = QStringLiteral("timed out");
    }
    return {*result, timer.elapsed()};
}

} // namespace

class TestVerticalCJKOCRBenchmark : public QObject
{
    Q_OBJECT
private slots:
    void compareNativeAndVerticalRetry();
};

void TestVerticalCJKOCRBenchmark::compareNativeAndVerticalRetry()
{
    if (!qEnvironmentVariableIsSet("SNAPTRAY_TEST_NATIVE_OCR")) {
        QSKIP("Set SNAPTRAY_TEST_NATIVE_OCR=1 to run the native OCR benchmark");
    }

    struct Sample {
        const char* name;
        QString languagePrefix;
        QString text;
        VerticalCJKSamples::Style style;
    };
    using Style = VerticalCJKSamples::Style;
    const QString hant = QStringLiteral("春眠不覺曉處處聞啼鳥");
    const QString hans = QStringLiteral("床前明月光疑是地上霜");
    const QString ja = QStringLiteral("吾輩は猫である名前はまだ無い");
    const std::vector<Sample> samples{
        {"zh-Hant", QStringLiteral("zh-Hant"), hant, Style{}},
        {"zh-Hans", QStringLiteral("zh-Hans"), hans, Style{}},
        {"ja", QStringLiteral("ja"), ja, Style{}},
        {"zh-Hant-strokes", QStringLiteral("zh-Hant"), QStringLiteral("三川小六二八"), Style{}},
        {"zh-Hant-punct", QStringLiteral("zh-Hant"), QStringLiteral("你好，世界。"), Style{}},
        {"zh-Hant-dark", QStringLiteral("zh-Hant"), hant, Style{40, QColor(235, 235, 235), QColor(30, 30, 30)}},
        {"zh-Hant-lowcontrast", QStringLiteral("zh-Hant"), hant, Style{40, QColor(160, 160, 160), QColor(205, 205, 205)}},
        {"zh-Hant-small", QStringLiteral("zh-Hant"), hant, Style{16}},
        {"zh-Hant-large", QStringLiteral("zh-Hant"), hant, Style{96}},
        {"ja-small", QStringLiteral("ja"), ja, Style{16}},
    };

    const QString outDir = qEnvironmentVariable("SNAPTRAY_OCR_BENCHMARK_DIR");
    QFile tsv;
    QTextStream out;
    if (!outDir.isEmpty()) {
        QVERIFY(QDir().mkpath(outDir));
        tsv.setFileName(QDir(outDir).filePath(QStringLiteral("results.tsv")));
        QVERIFY(tsv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        out.setDevice(&tsv);
        out << "sample\texpected\tnative_text\tnative_cer\tnative_ms\tretry_text\tretry_cer\tretry_ms\n";
    }

    int measured = 0;
    double nativeTotal = 0;
    double retryTotal = 0;
    for (const Sample& sample : samples) {
        const QString language = installedLanguage(sample.languagePrefix);
        const QString family = VerticalCJKSamples::cjkFamily(sample.text);
        if (language.isEmpty() || family.isEmpty()) {
            qInfo("%s: skipped (no OCR language or font)", sample.name);
            continue;
        }
        OCRManager manager;
        manager.setRecognitionLanguages({language});
        const QImage source = VerticalCJKSamples::renderVertical(sample.text, family, sample.style);
        const Timed native = recognize(manager, source);

        QElapsedTimer prepareTimer;
        prepareTimer.start();
        const auto normalized = VerticalCJKNormalizer::normalize(source);
        const qint64 prepareMs = prepareTimer.elapsed();
        QVERIFY2(normalized.isValid(), sample.name);
        const Timed retry = recognize(manager, normalized.image);

        // Only "no text" counts as an empty reading. A timeout or engine error
        // is not an accuracy result and must not enter the mean.
        for (const Timed* timed : {&native, &retry}) {
            const OCRResult& r = timed->result;
            QVERIFY2(r.success || r.error == ocrNoTextDetectedError(),
                     qPrintable(QStringLiteral("%1: OCR failed: %2")
                                    .arg(QLatin1String(sample.name), r.error)));
        }

        const double nativeCer = characterErrorRate(native.result.text, sample.text);
        const double retryCer = characterErrorRate(retry.result.text, sample.text);
        nativeTotal += nativeCer;
        retryTotal += retryCer;
        ++measured;
        qInfo("%-20s native CER %.2f (%lld ms) | retry CER %.2f (%lld+%lld ms) | native=\"%s\" retry=\"%s\"",
              sample.name, nativeCer, native.ms, retryCer, prepareMs, retry.ms,
              qPrintable(withoutWhitespace(native.result.text)), qPrintable(withoutWhitespace(retry.result.text)));

        if (!outDir.isEmpty()) {
            const QDir dir(outDir);
            source.save(dir.filePath(QStringLiteral("%1-source.png").arg(QLatin1String(sample.name))));
            normalized.image.save(dir.filePath(QStringLiteral("%1-retry.png").arg(QLatin1String(sample.name))));
            out << sample.name << '\t' << sample.text << '\t' << withoutWhitespace(native.result.text) << '\t'
                << nativeCer << '\t' << native.ms << '\t' << withoutWhitespace(retry.result.text) << '\t'
                << retryCer << '\t' << (prepareMs + retry.ms) << '\n';
        }
    }
    if (measured == 0) {
        QSKIP("No CJK OCR language or font installed");
    }
    qInfo("mean CER over %d samples: native %.3f, retry %.3f", measured, nativeTotal / measured,
          retryTotal / measured);
}

QTEST_MAIN(TestVerticalCJKOCRBenchmark)
#include "tst_VerticalCJKOCRBenchmark.moc"
