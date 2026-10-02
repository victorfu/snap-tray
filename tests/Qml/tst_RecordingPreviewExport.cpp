#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "qml/RecordingPreviewBackend.h"
#include "video/IVideoTranscoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <mutex>

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 12;
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
const QSize kFrameSize(64, 48);
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioFramesPerVideoFrame = kAudioSampleRate / kFrameRate;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerSample = 2;

// With audio, only the first `audioFrameCount` video frames get audio; the
// rest are video only, as when audio capture stops before the recording does.
QString createRecording(const QString& path, qint64 firstFrameMs, const QSize& frameSize = kFrameSize,
                        bool withAudio = false, int audioFrameCount = kFrameCount)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) {
        return QStringLiteral("No native encoder");
    }
    if (withAudio) encoder->setAudioFormat(kAudioSampleRate, kAudioChannels, kAudioBytesPerSample * 8);
    if (!encoder->start(path, frameSize, kFrameRate)) {
        return encoder->lastError();
    }
    // AAC is available on every supported macOS and Windows host; a fixture
    // that silently lost its audio track would make the audio assertions
    // meaningless.
    if (withAudio && !encoder->isAudioEnabled()) {
        encoder->abort();
        return QStringLiteral("Native encoder did not enable audio for the fixture");
    }

    QImage frame(frameSize, QImage::Format_ARGB32);
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
        if (withAudio && encoder->isAudioEnabled() && i < audioFrameCount) {
            encoder->writeAudioSamples(
                QByteArray(kAudioFramesPerVideoFrame * kAudioChannels * kAudioBytesPerSample, '\0'),
                qint64(i) * kAudioFramesPerVideoFrame);
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

// A transcoder whose transcode() misreports its result, so the backend's own
// output validation is the only thing standing between it and source deletion.
// probe() is the real native probe.
enum class FakeTranscodeOutcome {
    SilentOutputClaimsAudio,  // success, audioCopied, output has no audio track
    AudioNotCopied,           // success, real output with audio, audioCopied=false
    MissingOutput,            // success, audioCopied, nothing written
    WaitForCancel,            // reports progress until the callback cancels
    VideoOnlyRange,           // success, silent output, audioCopied=false: the range lies past the source audio
    IgnoresCrop,              // success, audioCopied, output is the uncropped source
    Real,                     // the native transcoder
};

constexpr int kFakeCancelWaitMs = 10000;
constexpr int kFakeCancelPollMs = 5;

struct FakeTranscodeLog {
    std::mutex mutex;
    int calls = 0;
    VideoTranscodeRequest lastRequest;
    std::atomic_bool started{false};
    std::atomic_bool cancelObserved{false};
    std::atomic_bool finished{false};
};

class FakeTranscoder : public IVideoTranscoder
{
public:
    FakeTranscoder(FakeTranscodeOutcome outcome, QString silentFixturePath,
                   std::shared_ptr<FakeTranscodeLog> log)
        : m_outcome(outcome)
        , m_silentFixturePath(std::move(silentFixturePath))
        , m_log(std::move(log))
        , m_real(IVideoTranscoder::create())
    {
    }

    VideoTranscodeResult transcode(const VideoTranscodeRequest& request,
                                   const ProgressCallback& progress) override
    {
        {
            std::lock_guard<std::mutex> lock(m_log->mutex);
            ++m_log->calls;
            m_log->lastRequest = request;
        }
        m_log->started.store(true);
        const auto finished = qScopeGuard([this]() { m_log->finished.store(true); });
        VideoTranscodeResult result;
        switch (m_outcome) {
        case FakeTranscodeOutcome::SilentOutputClaimsAudio:
            result.success = QFile::copy(m_silentFixturePath, request.outputPath);
            result.audioCopied = true;
            break;
        case FakeTranscodeOutcome::AudioNotCopied:
            result = m_real->transcode(request, progress);
            result.audioCopied = false;
            break;
        case FakeTranscodeOutcome::MissingOutput:
            result.success = true;
            result.audioCopied = true;
            break;
        case FakeTranscodeOutcome::WaitForCancel: {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < kFakeCancelWaitMs) {
                if (!progress(0)) {
                    m_log->cancelObserved.store(true);
                    break;
                }
                QThread::msleep(kFakeCancelPollMs);
            }
            result.errorMessage = QStringLiteral("cancelled");
            break;
        }
        case FakeTranscodeOutcome::VideoOnlyRange:
            result.success = QFile::copy(m_silentFixturePath, request.outputPath);
            result.audioCopied = false;
            break;
        case FakeTranscodeOutcome::IgnoresCrop:
            result.success = QFile::copy(request.inputPath, request.outputPath);
            result.audioCopied = true;
            break;
        case FakeTranscodeOutcome::Real:
            result = m_real->transcode(request, progress);
            break;
        }
        return result;
    }

    VideoFileProbe probe(const QString& filePath) override { return m_real->probe(filePath); }

private:
    FakeTranscodeOutcome m_outcome;
    QString m_silentFixturePath;
    std::shared_ptr<FakeTranscodeLog> m_log;
    std::unique_ptr<IVideoTranscoder> m_real;
};

QStringList partFiles(const QString& directoryPath)
{
    return QDir(directoryPath).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files);
}

} // namespace

class tst_RecordingPreviewExport : public QObject
{
    Q_OBJECT

private slots:
    void closeOutcomes_data();
    void closeOutcomes();
    void saveAnimation_data();
    void saveAnimation();
    void saveCroppedAnimation_data();
    void saveCroppedAnimation();
    void saveCroppedAnimationExceedsBounds_data();
    void saveCroppedAnimationExceedsBounds();
    void failedExportPreservesOriginal_data();
    void failedExportPreservesOriginal();
    void saveMp4Edits_data();
    void saveMp4Edits();
    void videoOnlyExportAcceptedWhenRangeHasNoSourceAudio();
    void invalidTranscodeKeepsSourceAndRetries_data();
    void invalidTranscodeKeepsSourceAndRetries();
    void destroyWhileExportingKeepsSource_data();
    void destroyWhileExportingKeepsSource();
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
    // Uncropped: no output size, so the filename keeps the recording's size.
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
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

void tst_RecordingPreviewExport::saveCroppedAnimation_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::saveCroppedAnimation()
{
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(32, 24, 96, 72);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.setCropRect(crop);
    QCOMPARE(backend.cropRect(), crop);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    // The filename's {w}x{h} must describe the cropped output.
    QCOMPARE(savedSpy.first().at(1).toSize(), crop.size());

    QImageReader reader(savedSpy.first().at(0).toString());
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    const QImage first = reader.read();
    QCOMPARE(first.size(), crop.size());
    QVERIFY(isRed(first.pixelColor(first.rect().center())));
    QVERIFY(QDir(directory.path()).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files).isEmpty());
}

void tst_RecordingPreviewExport::saveCroppedAnimationExceedsBounds_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::saveCroppedAnimationExceedsBounds()
{
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QSize reportedSize(200, 150);  // Larger than actual
    const QRect crop(80, 60, 96, 72);   // Valid for reported size, exceeds actual

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    // Report larger size than actual video
    backend.updateVideoSize(reportedSize);
    // Set crop that exceeds actual frame bounds
    backend.setCropRect(crop);
    QCOMPARE(backend.cropRect(), crop);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    // Should have failed due to crop exceeding frame bounds
    QVERIFY(!backend.errorMessage().isEmpty());
    QCOMPARE(savedSpy.count(), 0);
    // Source file should still exist (not deleted)
    QVERIFY(QFileInfo::exists(inputPath));
    // No partial files should remain
    QVERIFY(QDir(directory.path()).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files).isEmpty());
}

void tst_RecordingPreviewExport::saveMp4Edits_data()
{
    QTest::addColumn<bool>("trim");
    QTest::addColumn<bool>("crop");
    QTest::addColumn<bool>("withAudio");
    QTest::addColumn<int>("audioFrameCount");
    QTest::addColumn<bool>("expectOutputAudio");
    QTest::newRow("trim-with-audio") << true << false << true << kFrameCount << true;
    QTest::newRow("crop-with-audio") << false << true << true << kFrameCount << true;
    QTest::newRow("trim-and-crop-silent") << true << true << false << kFrameCount << false;
    // Audio stops at 200 ms; the trim (400 ms on) lies entirely past it, so
    // the export is video only, like the source is there.
    QTest::newRow("trim-after-audio-end") << true << false << true << 2 << false;
}

void tst_RecordingPreviewExport::saveMp4Edits()
{
    QFETCH(bool, trim);
    QFETCH(bool, crop);
    QFETCH(bool, withAudio);
    QFETCH(int, audioFrameCount);
    QFETCH(bool, expectOutputAudio);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);
    const qint64 trimStartMs = 4 * kFrameIntervalMs;
    const qint64 trimEndMs = 10 * kFrameIntervalMs;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, withAudio, audioFrameCount);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe sourceProbe = transcoder->probe(inputPath);
    QCOMPARE(sourceProbe.hasAudio, withAudio);
    if (withAudio && audioFrameCount < kFrameCount) {
        // The fixture's audio really ends before the trimmed range starts.
        QVERIFY2(sourceProbe.audioEndMs >= 0 && sourceProbe.audioEndMs < trimStartMs,
                 qPrintable(QString::number(sourceProbe.audioEndMs)));
    }

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (trim) {
        backend.setTrimStart(trimStartMs);
        backend.setTrimEnd(trimEndMs);
    }
    if (crop) {
        backend.setCropRect(cropRect);
    }

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QVERIFY(backend.isProcessing());
    // A second save while the export runs must not start another export.
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(backend.processProgress(), 100);

    const QString outputPath = savedSpy.first().at(0).toString();
    QCOMPARE(savedSpy.first().at(1).toSize(), crop ? cropRect.size() : QSize());
    QCOMPARE(QFileInfo(outputPath).suffix(), QStringLiteral("mp4"));
    QCOMPARE(QFileInfo(outputPath).absolutePath(), directory.path());
    QVERIFY(!QFileInfo::exists(inputPath));
    const VideoFileProbe probe = transcoder->probe(outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, crop ? cropRect.size() : sourceSize);
    QCOMPARE(probe.hasAudio, expectOutputAudio);
    if (trim) {
        QVERIFY2(qAbs(probe.durationMs - (trimEndMs - trimStartMs)) <= 2 * kFrameIntervalMs,
                 qPrintable(QString::number(probe.durationMs)));
    }
    QVERIFY(partFiles(directory.path()).isEmpty());
    // Only the promoted output remains.
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList(QFileInfo(outputPath).fileName()));
}

// A transcoder that returns success with audioCopied=false and a silent
// output is right when the selected range starts after the source audio
// ends. The backend's own validation must accept that instead of treating
// it as lost audio, or the tail of such a recording can never be exported.
void tst_RecordingPreviewExport::videoOnlyExportAcceptedWhenRangeHasNoSourceAudio()
{
    const QSize sourceSize(160, 120);
    const qint64 trimStartMs = 4 * kFrameIntervalMs;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    // Audio for the first two frames (200 ms) of a 1.2 s recording.
    QString fixtureError = createRecording(inputPath, 0, sourceSize, true, 2);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    QTemporaryDir fixtureDirectory;
    QVERIFY(fixtureDirectory.isValid());
    const QString silentPath = fixtureDirectory.filePath(QStringLiteral("silent.mp4"));
    fixtureError = createRecording(silentPath, 0, sourceSize, false);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto realTranscoder = IVideoTranscoder::create();
    QVERIFY(realTranscoder);
    const VideoFileProbe sourceProbe = realTranscoder->probe(inputPath);
    QVERIFY(sourceProbe.valid);
    QVERIFY(sourceProbe.hasAudio);
    QVERIFY2(sourceProbe.audioEndMs >= 0 && sourceProbe.audioEndMs < trimStartMs,
             qPrintable(QString::number(sourceProbe.audioEndMs)));

    auto restoreFactory = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [silentPath, log]() {
        return std::unique_ptr<IVideoTranscoder>(
            new FakeTranscoder(FakeTranscodeOutcome::VideoOnlyRange, silentPath, log));
    };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setTrimStart(trimStartMs);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    {
        std::lock_guard<std::mutex> lock(log->mutex);
        QCOMPARE(log->calls, 1);
        QCOMPARE(log->lastRequest.startMs, trimStartMs);
    }
    QVERIFY(!QFileInfo::exists(inputPath));
    const VideoFileProbe outputProbe = realTranscoder->probe(savedSpy.first().at(0).toString());
    QVERIFY(outputProbe.valid);
    QVERIFY(!outputProbe.hasAudio);
    QVERIFY(partFiles(directory.path()).isEmpty());
}

void tst_RecordingPreviewExport::invalidTranscodeKeepsSourceAndRetries_data()
{
    QTest::addColumn<int>("outcome");
    QTest::newRow("silent-output-claims-audio") << int(FakeTranscodeOutcome::SilentOutputClaimsAudio);
    // Right audio, wrong picture: the crop was not applied.
    QTest::newRow("ignores-crop") << int(FakeTranscodeOutcome::IgnoresCrop);
    QTest::newRow("audio-not-copied") << int(FakeTranscodeOutcome::AudioNotCopied);
    QTest::newRow("missing-output") << int(FakeTranscodeOutcome::MissingOutput);
}

void tst_RecordingPreviewExport::invalidTranscodeKeepsSourceAndRetries()
{
    QFETCH(int, outcome);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    QString fixtureError = createRecording(inputPath, 0, sourceSize, true);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = input.readAll();
    input.close();

    QTemporaryDir fixtureDirectory;
    QVERIFY(fixtureDirectory.isValid());
    const QString silentPath = fixtureDirectory.filePath(QStringLiteral("silent.mp4"));
    fixtureError = createRecording(silentPath, 0, cropRect.size(), false);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto realTranscoder = IVideoTranscoder::create();
    QVERIFY(realTranscoder);
    QVERIFY(realTranscoder->probe(inputPath).hasAudio);
    QVERIFY(realTranscoder->probe(silentPath).valid);
    QVERIFY(!realTranscoder->probe(silentPath).hasAudio);

    auto restoreFactory = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    const auto fakeOutcome = static_cast<FakeTranscodeOutcome>(outcome);
    RecordingPreviewBackend::transcoderFactoryOverride() = [fakeOutcome, silentPath, log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(fakeOutcome, silentPath, log));
    };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setCropRect(cropRect);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    {
        std::lock_guard<std::mutex> lock(log->mutex);
        QCOMPARE(log->calls, 1);
        QCOMPARE(log->lastRequest.inputPath, inputPath);
        QCOMPARE(log->lastRequest.cropRect, cropRect);
        QCOMPARE(log->lastRequest.startMs, qint64(0));
        QCOMPARE(log->lastRequest.endMs, qint64(-1));
        QVERIFY2(log->lastRequest.outputPath.contains(QStringLiteral(".part-")),
                 qPrintable(log->lastRequest.outputPath));
    }
    // The user sees a translated summary; the English diagnostics go to the log.
    QCOMPARE(backend.errorMessage(),
             RecordingPreviewBackend::tr("Export failed; the original recording was kept."));
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), originalBytes);
    input.close();
    QVERIFY(partFiles(directory.path()).isEmpty());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList(QStringLiteral("recording.mp4")));

    // The failure leaves the export retryable with the real transcoder.
    RecordingPreviewBackend::transcoderFactoryOverride() = {};
    backend.clearError();
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(!QFileInfo::exists(inputPath));
    const VideoFileProbe probe = realTranscoder->probe(savedSpy.first().at(0).toString());
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, cropRect.size());
    QVERIFY(probe.hasAudio);
    QVERIFY(partFiles(directory.path()).isEmpty());
}

void tst_RecordingPreviewExport::destroyWhileExportingKeepsSource_data()
{
    QTest::addColumn<int>("outcome");
    // Destroyed mid-transcode: the cancel token must stop the worker.
    QTest::newRow("during-transcode") << int(FakeTranscodeOutcome::WaitForCancel);
    // Destroyed after the output was promoted: the queued completion must
    // drop the output instead of touching the backend or deleting the source.
    QTest::newRow("after-promotion") << int(FakeTranscodeOutcome::Real);
}

void tst_RecordingPreviewExport::destroyWhileExportingKeepsSource()
{
    QFETCH(int, outcome);
    const auto fakeOutcome = static_cast<FakeTranscodeOutcome>(outcome);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, true);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = input.readAll();
    input.close();

    auto restoreFactory = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [fakeOutcome, log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(fakeOutcome, QString(), log));
    };

    auto backend = std::make_unique<RecordingPreviewBackend>(inputPath);
    backend->setSelectedFormat(RecordingPreviewBackend::MP4);
    backend->updateVideoSize(sourceSize);
    backend->updateDuration(kFrameCount * kFrameIntervalMs);
    backend->setCropRect(cropRect);
    QSignalSpy savedSpy(backend.get(), &RecordingPreviewBackend::saveRequested);
    backend->save();
    QVERIFY(backend->isProcessing());

    // Wait without spinning the event loop, so the worker's queued completion
    // cannot reach the backend before it is destroyed.
    const auto waitFor = [](const std::function<bool()>& condition) {
        QElapsedTimer timer;
        timer.start();
        while (!condition() && timer.elapsed() < kFakeCancelWaitMs) {
            QThread::msleep(kFakeCancelPollMs);
        }
        return condition();
    };
    const QString directoryPath = directory.path();
    if (fakeOutcome == FakeTranscodeOutcome::Real) {
        // The promoted output exists only after validation passed, so the
        // destructor's cancel can no longer reach the worker; only the queued
        // completion's QPointer check stands between it and the source.
        QVERIFY(waitFor([log]() { return log->finished.load(); }));
        QVERIFY(waitFor([directoryPath]() {
            const QStringList edited = QDir(directoryPath).entryList(
                QStringList(QStringLiteral("recording_edited_*.mp4")), QDir::Files);
            return !edited.isEmpty() && partFiles(directoryPath).isEmpty();
        }));
    } else {
        QVERIFY(waitFor([log]() { return log->started.load(); }));
    }

    backend.reset();
    QVERIFY(waitFor([log]() { return log->finished.load(); }));
    if (fakeOutcome == FakeTranscodeOutcome::WaitForCancel) {
        QVERIFY(log->cancelObserved.load());
    }

    QTRY_COMPARE_WITH_TIMEOUT(QDir(directory.path()).entryList(QDir::Files),
                              QStringList(QStringLiteral("recording.mp4")), 20000);
    QCOMPARE(savedSpy.count(), 0);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), originalBytes);
}

QTEST_MAIN(tst_RecordingPreviewExport)
#include "tst_RecordingPreviewExport.moc"

void tst_RecordingPreviewExport::closeOutcomes_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("close") << 0;
    QTest::newRow("discard") << 1;
    QTest::newRow("save") << 2;
    QTest::newRow("processing-close") << 3;
}

void tst_RecordingPreviewExport::closeOutcomes()
{
    QFETCH(int, action);
    RecordingPreviewBackend backend("temporary.mp4");
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QSignalSpy saved(&backend, &RecordingPreviewBackend::saveRequested);
    if (action == 3) backend.m_isProcessing = true;
    if (action == 1) backend.discard();
    else if (action == 2) backend.save();
    else backend.close();
    QCOMPARE(closed.count(), action == 3 ? 0 : 1);
    QCOMPARE(discarded.count(), action < 2 ? 1 : 0);
    QCOMPARE(saved.count(), action == 2 ? 1 : 0);
    if (action == 2) {
        QCOMPARE(saved.first().at(1).toSize(), QSize());
    }
    if (action == 3) {
        backend.m_isProcessing = false;
        backend.close();
    }
    QCOMPARE(closed.first().first().toBool(), action == 2);
    backend.close();
    backend.discard();
    backend.save();
    QCOMPARE(closed.count(), 1);
    QCOMPARE(discarded.count(), action == 2 ? 0 : 1);
    QCOMPARE(saved.count(), action == 2 ? 1 : 0);
}
