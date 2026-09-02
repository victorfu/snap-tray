#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 12;
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
const QSize kFrameSize(64, 48);

QString createRecording(const QString& path, qint64 firstFrameMs)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->start(path, kFrameSize, kFrameRate)) {
        return encoder ? encoder->lastError() : QStringLiteral("No native encoder");
    }

    QImage frame(kFrameSize, QImage::Format_ARGB32);
    for (int i = 0; i < kFrameCount; ++i) {
        frame.fill(i < 4 ? Qt::red : i < 8 ? Qt::green : Qt::blue);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer waitTimer;
        waitTimer.start();
        // The recording encoder accepts real-time input and may initially apply
        // backpressure. Retry the same timestamp until this fixture frame lands.
        do {
            encoder->writeFrame(frame, firstFrameMs + i * kFrameIntervalMs);
            if (encoder->framesWritten() != before) {
                break;
            }
            QTest::qWait(5);
        } while (waitTimer.elapsed() < 2000);

        if (encoder->framesWritten() != before + 1) {
            return QStringLiteral("Native encoder did not accept fixture frame %1: %2")
                .arg(i).arg(encoder->lastError());
        }
    }

    // AVFoundation finalizes asynchronously on the main queue. Keep the encoder
    // alive and service that queue before using the MP4 or destroying the writer.
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) {
        return QStringLiteral("Native encoder did not finish the fixture");
    }
    if (!finishedSpy.first().at(0).toBool()) {
        return QStringLiteral("Native encoder failed to finish: %1").arg(encoder->lastError());
    }
    if (finishedSpy.first().at(1).toString() != path || QFileInfo(path).size() <= 0) {
        return QStringLiteral("Native encoder produced no fixture file");
    }
    return {};
}

bool isRed(const QColor& color)
{
    return color.red() > 180 && color.green() < 60 && color.blue() < 60;
}

bool isGreen(const QColor& color)
{
    return color.green() > 180 && color.red() < 60 && color.blue() < 60;
}

bool isBlue(const QColor& color)
{
    return color.blue() > 180 && color.red() < 60 && color.green() < 60;
}

} // namespace

class tst_RecordingPreviewExport : public QObject
{
    Q_OBJECT

private slots:
    void saveAnimation_data();
    void saveAnimation();
    void failedExportPreservesOriginal_data();
    void failedExportPreservesOriginal();
};

void tst_RecordingPreviewExport::saveAnimation_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<qint64>("firstFrameMs");
    QTest::addColumn<bool>("trimmed");

    QTest::newRow("gif-zero-start") << int(RecordingPreviewBackend::GIF) << qint64(0) << false;
    QTest::newRow("webp-zero-start") << int(RecordingPreviewBackend::WebP) << qint64(0) << false;
    QTest::newRow("gif-delayed-start") << int(RecordingPreviewBackend::GIF) << qint64(100) << false;
    QTest::newRow("webp-delayed-start") << int(RecordingPreviewBackend::WebP) << qint64(100) << false;
    QTest::newRow("gif-trimmed") << int(RecordingPreviewBackend::GIF) << qint64(100) << true;
    QTest::newRow("webp-trimmed") << int(RecordingPreviewBackend::WebP) << qint64(100) << true;
}

void tst_RecordingPreviewExport::saveAnimation()
{
    QFETCH(int, format);
    QFETCH(qint64, firstFrameMs);
    QFETCH(bool, trimmed);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, firstFrameMs);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    if (trimmed) {
        backend.updateDuration(firstFrameMs + kFrameCount * kFrameIntervalMs);
        backend.setTrimStart(firstFrameMs + 4 * kFrameIntervalMs);
        backend.setTrimEnd(firstFrameMs + 11 * kFrameIntervalMs);
        QVERIFY(backend.hasTrim());
    }
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);

    const QString outputPath = savedSpy.first().at(0).toString();
    const QByteArray expectedFormat = format == RecordingPreviewBackend::GIF ? "gif" : "webp";
    QCOMPARE(QFileInfo(outputPath).absolutePath(), directory.path());
    QCOMPARE(QFileInfo(outputPath).suffix().toLatin1(), expectedFormat);
    QVERIFY(QFileInfo(outputPath).size() > 0);
    QVERIFY(!QFileInfo::exists(inputPath));

    // Decode the saved animation, so success cannot be satisfied by an empty
    // container, one repeated still frame, or a conversion that loses its tail.
    QImageReader reader(outputPath);
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    QCOMPARE(reader.format(), expectedFormat);
    QVERIFY(reader.supportsAnimation());
    QVERIFY(reader.imageCount() > 1);

    QColor firstColor;
    QColor lastColor;
    bool sawGreen = false;
    qint64 animationDurationMs = 0;
    for (int i = 0; i < reader.imageCount(); ++i) {
        const QImage frame = reader.read();
        QVERIFY2(!frame.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(frame.size(), kFrameSize);
        animationDurationMs += reader.nextImageDelay();
        const QColor color = frame.pixelColor(frame.rect().center());
        if (i == 0) {
            firstColor = color;
        }
        lastColor = color;
        sawGreen = sawGreen || isGreen(color);
        if (trimmed) {
            QVERIFY2(!isRed(color), "The animation includes frames before the selected trim range");
        }
    }
    QVERIFY2(trimmed ? isGreen(firstColor) : isRed(firstColor),
             qPrintable(QStringLiteral("Unexpected first frame: %1").arg(firstColor.name())));
    QVERIFY(sawGreen);
    QVERIFY2(isBlue(lastColor),
             qPrintable(QStringLiteral("Unexpected last frame: %1").arg(lastColor.name())));
    const qint64 expectedDurationMs = trimmed
        ? 7 * kFrameIntervalMs
        : firstFrameMs + kFrameCount * kFrameIntervalMs;
    // Preserve the selected source timeline, including time before a delayed
    // first frame. Allow one sampling interval and GIF centisecond rounding.
    const qint64 durationToleranceMs = kFrameIntervalMs + (format == RecordingPreviewBackend::GIF ? 10 : 0);
    QVERIFY2(qAbs(animationDurationMs - expectedDurationMs) <= durationToleranceMs,
             qPrintable(QStringLiteral("Animation duration %1 ms differs from expected %2 ms")
                            .arg(animationDurationMs).arg(expectedDurationMs)));
    QVERIFY(QDir(directory.path()).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files).isEmpty());
}

void tst_RecordingPreviewExport::failedExportPreservesOriginal_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::failedExportPreservesOriginal()
{
    QFETCH(int, format);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("invalid.mp4"));
    const QByteArray originalBytes("Incomplete recording data");
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    QCOMPARE(input.write(originalBytes), originalBytes.size());
    input.close();

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY(!backend.errorMessage().isEmpty());
    QCOMPARE(savedSpy.count(), 0);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), originalBytes);
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList(QStringLiteral("invalid.mp4")));
}

QTEST_MAIN(tst_RecordingPreviewExport)
#include "tst_RecordingPreviewExport.moc"
