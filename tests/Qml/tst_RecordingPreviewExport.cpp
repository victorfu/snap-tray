#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "qml/RecordingPreviewBackend.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "recording/WindowTimeline.h"
#include "recording/WindowTimelineSidecar.h"
#include "video/IVideoTranscoder.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QWindow>
#include <QGuiApplication>

#include <atomic>
#include <memory>
#include <mutex>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

// A recording the encoder cannot compress: random pixels at quality 100,
// large enough that its average bitrate sits far above the quality-0 target
// (1280x720 @ kFrameRate: target 1,000,000 bps after the kMinBitrate floor,
// encoder budget 2,764,800 bps). Used to force the smart-save re-encode branch.
const QSize kNoiseSize(1280, 720);
constexpr int kNoiseFrameCount = 40;
constexpr quint32 kNoiseSeed = 20261003;

QString createNoiseRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(SnapTray::VideoBitrate::kMaxQuality);
    if (!encoder->start(path, kNoiseSize, kFrameRate)) return encoder->lastError();
    QRandomGenerator random(kNoiseSeed);
    QImage frame(kNoiseSize, QImage::Format_ARGB32);
    for (int i = 0; i < kNoiseFrameCount; ++i) {
        auto* pixels = reinterpret_cast<quint32*>(frame.bits());
        const qsizetype count = qsizetype(frame.width()) * frame.height();
        for (qsizetype p = 0; p < count; ++p) pixels[p] = 0xFF000000u | (random.generate() & 0x00FFFFFFu);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer waitTimer;
        waitTimer.start();
        do {
            encoder->writeFrame(frame, i * kFrameIntervalMs);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (waitTimer.elapsed() < 2000);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("noise frame %1 rejected").arg(i);
    }
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) return QStringLiteral("noise fixture did not finish");
    return finishedSpy.first().at(0).toBool() ? QString() : encoder->lastError();
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

// The destructor runs after the worker has queued its completion callback.
// Waiting on this semaphore without pumping GUI events exposes the commit race.
constexpr int kExportGateTimeoutMs = 5000;

struct ExportGate {
    QSemaphore probeEntered;
    QSemaphore resumeProbe;
    QSemaphore workerFinished;
    std::atomic_bool firstProbe{true};
    std::atomic_bool timedOut{false};
    bool blockProbe = false;
    bool failTranscode = false;
};

class GatedTranscoder : public IVideoTranscoder
{
public:
    explicit GatedTranscoder(std::shared_ptr<ExportGate> gate) : m_gate(std::move(gate)) {}
    ~GatedTranscoder() override { m_gate->workerFinished.release(); }
    VideoFileProbe probe(const QString&) override
    {
        if (m_gate->blockProbe && m_gate->firstProbe.exchange(false)) {
            m_gate->probeEntered.release();
            if (!m_gate->resumeProbe.tryAcquire(1, kExportGateTimeoutMs)) m_gate->timedOut.store(true);
        }
        VideoFileProbe result;
        result.valid = true;
        result.videoSize = QSize(160, 120);
        result.durationMs = 1000;
        result.frameRate = 10;
        result.videoCodec = QString::fromLatin1(m_gate->failTranscode ? kVideoCodecHevc : kVideoCodecH264);
        return result;
    }
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback&) override
    {
        VideoTranscodeResult result;
        if (m_gate->failTranscode) result.errorMessage = QStringLiteral("injected failure");
        else result.success = QFile::copy(request.inputPath, request.outputPath);
        return result;
    }
private:
    std::shared_ptr<ExportGate> m_gate;
};

} // namespace

class tst_RecordingPreviewExport : public QObject
{
    Q_OBJECT

private slots:
    void closeCancelsLongshotBeforeDiscard();
    void closeOutcomes_data();
    void closeOutcomes();
    void previewWindowKeepsNativeCaption();
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
    void saveMp4EditsUseOutputQuality_data();
    void saveMp4EditsUseOutputQuality();
    void smartSaveMovesLowBitrateRecording();
    void smartSaveReencodesHighBitrateRecording();
    void smartSaveMovesUnprobeableRecording();
    void smartSaveMovesRecordingNotRecordedAsIntermediate();
    void failedSmartSaveReencodeSavesOriginal();
    void cancelExportKeepsSource();
    void cancelBeforeGuiCommit_data();
    void cancelBeforeGuiCommit();
    void videoOnlyExportAcceptedWhenRangeHasNoSourceAudio();
    void invalidTranscodeKeepsSourceAndRetries_data();
    void invalidTranscodeKeepsSourceAndRetries();
    void destroyWhileExportingKeepsSource_data();
    void destroyWhileExportingKeepsSource();
};

void tst_RecordingPreviewExport::closeCancelsLongshotBeforeDiscard()
{
    if (!SnapTray::Longshot::FrameReaderLongshotSource::createNative()) QSKIP("No native offline reader");
    QTemporaryDir directory;
    const QString path = directory.filePath("longshot-close.mp4");
    const QString error = createRecording(path, 0);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    RecordingPreviewBackend backend(path);
    backend.updateDuration(1000);
    backend.setSelectedFormat(RecordingPreviewBackend::LongScreenshot);
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    bool removedAfterIdle = false;
    connect(&backend, &RecordingPreviewBackend::discardRequested, this, [&](const QString& source) {
        removedAfterIdle = !backend.longshot()->busy() && QFile::remove(source);
    });
    backend.save();
    QVERIFY(backend.longshot()->busy());
    backend.close();
    QCOMPARE(closed.count(), 0);
    QVERIFY(QFile::exists(path));
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
    QVERIFY(removedAfterIdle);
    QVERIFY(!QFile::exists(path));
    QVERIFY(!backend.longshot()->hasResult());
}

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
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(kFrameSize);
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));

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
    QVERIFY(!backend.canCancelExport());
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QCOMPARE(backend.selectedFormat(), format);
    const QString status = backend.processStatus();
    backend.cancelExport();
    QCOMPARE(backend.processStatus(), status);
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
    // The source recording and its sidecar are replaced by the export.
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(inputPath)));
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
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(sourceSize);
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));

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

void tst_RecordingPreviewExport::saveMp4EditsUseOutputQuality_data()
{
    QTest::addColumn<bool>("trim");
    QTest::addColumn<bool>("crop");
    QTest::addColumn<int>("quality");
    QTest::newRow("trim-only low") << true << false << 0;
    QTest::newRow("trim-only high") << true << false << 100;
    QTest::newRow("crop-only low") << false << true << 0;
    QTest::newRow("crop-only high") << false << true << 100;
    QTest::newRow("trim+crop mid") << true << true << 55;
}

void tst_RecordingPreviewExport::saveMp4EditsUseOutputQuality()
{
    QFETCH(bool, trim);
    QFETCH(bool, crop);
    QFETCH(int, quality);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);
    const qint64 trimStartMs = 300;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, false);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = [quality]() { return quality; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (trim) backend.setTrimStart(trimStartMs);
    if (crop) backend.setCropRect(cropRect);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QSize outputSize = crop ? cropRect.size() : sourceSize;
    const int expectedBitrate = SnapTray::IntermediateQuality::outputBitrateFor(outputSize, kFrameRate, quality);
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 1);
    QCOMPARE(log->lastRequest.videoBitrate, expectedBitrate);
    QVERIFY(log->lastRequest.videoBitrate > 0);
}

void tst_RecordingPreviewExport::smartSaveMovesLowBitrateRecording()
{
    // Solid-colour frames compress far below any target: the file is moved as is.
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, true);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(QFileInfo::exists(inputPath)); // the consumer of saveRequested moves it
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 0);
}

void tst_RecordingPreviewExport::smartSaveMovesUnprobeableRecording()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    {
        QByteArray garbage;
        QRandomGenerator random(kNoiseSeed);
        for (int i = 0; i < 4096; ++i) garbage.append(char(random.generate() & 0xFF));
        QFile file(inputPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(garbage), garbage.size());
    }

    auto restore = qScopeGuard([]() { RecordingPreviewBackend::transcoderFactoryOverride() = {}; });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(QSize(160, 120));
    backend.updateDuration(2000);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QVERIFY(QFileInfo::exists(inputPath));
    QVERIFY(backend.errorMessage().isEmpty());
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 0);
}

void tst_RecordingPreviewExport::smartSaveReencodesHighBitrateRecording()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createNoiseRecording(inputPath);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto realTranscoder = IVideoTranscoder::create();
    QVERIFY(realTranscoder);
    const VideoFileProbe sourceProbe = realTranscoder->probe(inputPath);
    QVERIFY(sourceProbe.valid);
    const int target = SnapTray::IntermediateQuality::outputBitrateFor(kNoiseSize, sourceProbe.frameRate, 0);
    const qint64 actual = SnapTray::IntermediateQuality::averageBitrate(QFileInfo(inputPath).size(), sourceProbe.durationMs);
    QVERIFY2(actual > target, qPrintable(QStringLiteral("fixture %1 bps is not above target %2").arg(actual).arg(target)));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(kNoiseSize);
    backend.updateDuration(kNoiseFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QVERIFY(backend.isProcessing());
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Exporting video..."));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 60000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString outputPath = savedSpy.first().at(0).toString();
    QVERIFY(outputPath != inputPath);
    QVERIFY(!QFileInfo::exists(inputPath));
    const VideoFileProbe outputProbe = realTranscoder->probe(outputPath);
    QVERIFY(outputProbe.valid);
    QCOMPARE(outputProbe.videoSize, kNoiseSize);
    QVERIFY(partFiles(directory.path()).isEmpty());
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 1);
    QCOMPARE(log->lastRequest.startMs, qint64(0));
    QCOMPARE(log->lastRequest.endMs, qint64(-1));
    QVERIFY(log->lastRequest.cropRect.isEmpty());
    QCOMPARE(log->lastRequest.videoBitrate, target);
}

void tst_RecordingPreviewExport::smartSaveMovesRecordingNotRecordedAsIntermediate()
{
    // Recorded at the selected quality (low disk): even a file above the
    // target is saved as recorded, never re-encoded a second time.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString noiseInputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createNoiseRecording(noiseInputPath);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(noiseInputPath, /*recordedAsIntermediate*/ false);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(kNoiseSize);
    backend.updateDuration(kNoiseFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), noiseInputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QVERIFY(QFileInfo::exists(noiseInputPath));
    QVERIFY(backend.errorMessage().isEmpty());
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 0);
}

void tst_RecordingPreviewExport::failedSmartSaveReencodeSavesOriginal()
{
    // The noise fixture is above the quality-0 target, so an unedited save
    // re-encodes; when that fails the recording is still saved as recorded.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createNoiseRecording(inputPath);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(
            new FakeTranscoder(FakeTranscodeOutcome::MissingOutput, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(kNoiseSize);
    backend.updateDuration(kNoiseFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QVERIFY(QFileInfo::exists(inputPath));
    QVERIFY(partFiles(directory.path()).isEmpty());
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 1);
}

void tst_RecordingPreviewExport::cancelExportKeepsSource()
{
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
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(
            new FakeTranscoder(FakeTranscodeOutcome::WaitForCancel, QString(), log));
    };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setCropRect(cropRect);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QVERIFY(backend.isProcessing());
    QTRY_VERIFY_WITH_TIMEOUT(log->started.load(), kFakeCancelWaitMs);

    backend.cancelExport();
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Cancelling..."));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), kFakeCancelWaitMs);
    QVERIFY(log->cancelObserved.load());
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(partFiles(directory.path()).isEmpty());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList(QStringLiteral("recording.mp4")));
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), originalBytes);
}

void tst_RecordingPreviewExport::cancelBeforeGuiCommit_data()
{
    QTest::addColumn<bool>("duringProbe");
    QTest::addColumn<bool>("edited");
    QTest::addColumn<bool>("failTranscode");
    QTest::newRow("direct-save-during-probe") << true << false << false;
    QTest::newRow("direct-save-queued") << false << false << false;
    QTest::newRow("transcode-queued") << false << true << false;
    QTest::newRow("fallback-during-probe") << true << false << true;
    QTest::newRow("fallback-queued") << false << false << true;
}

void tst_RecordingPreviewExport::cancelBeforeGuiCommit()
{
    QFETCH(bool, duringProbe);
    QFETCH(bool, edited);
    QFETCH(bool, failTranscode);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    const QByteArray original("original recording");
    QCOMPARE(input.write(original), qint64(original.size()));
    input.close();
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(QSize(160, 120));
    SnapTray::WindowSample window;
    window.rect = QRect(0, 0, 100, 100);
    timeline.append(0, {window});
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));
    const QStringList originalFiles = QDir(directory.path()).entryList(QDir::Files);

    const auto gate = std::make_shared<ExportGate>();
    gate->blockProbe = duringProbe;
    gate->failTranscode = failTranscode;
    auto restore = qScopeGuard([gate]() {
        gate->resumeProbe.release();
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
    });
    RecordingPreviewBackend::transcoderFactoryOverride() = [gate]() {
        return std::make_unique<GatedTranscoder>(gate);
    };
    RecordingPreviewBackend backend(inputPath);
    backend.updateDuration(1000);
    backend.updateVideoSize(QSize(160, 120));
    if (edited) backend.setTrimStart(100);
    QSignalSpy saved(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QVERIFY(!backend.canCancelExport());
    backend.save();
    QVERIFY(backend.isProcessing());
    QVERIFY(backend.canCancelExport());
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QCOMPARE(backend.selectedFormat(), int(RecordingPreviewBackend::MP4));
    if (duringProbe) QVERIFY(gate->probeEntered.tryAcquire(1, kExportGateTimeoutMs));
    else QVERIFY(gate->workerFinished.tryAcquire(1, kExportGateTimeoutMs));
    backend.cancelExport();
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Cancelling..."));
    QVERIFY(!backend.canCancelExport());
    QSignalSpy statusChanged(&backend, &RecordingPreviewBackend::processStatusChanged);
    backend.cancelExport();
    QCOMPARE(statusChanged.count(), 0);
    if (duringProbe) {
        gate->resumeProbe.release();
        QVERIFY(gate->workerFinished.tryAcquire(1, kExportGateTimeoutMs));
    }
    QVERIFY(!gate->timedOut.load());
    QTRY_VERIFY(!backend.isProcessing());
    QVERIFY(!backend.canCancelExport());
    QCOMPARE(saved.count(), 0);
    QCOMPARE(closed.count(), 0);
    QCOMPARE(discarded.count(), 0);
    QVERIFY(backend.errorMessage().isEmpty());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files), originalFiles);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), original);
    input.close();
    const auto keptTimeline = SnapTray::WindowTimelineSidecar::read(inputPath);
    QVERIFY(keptTimeline.has_value());
    QCOMPARE(keptTimeline->toJson(), timeline.toJson());

    // A fresh export must not inherit cancellation from the previous one.
    backend.save();
    QVERIFY(backend.canCancelExport());
    QVERIFY(gate->workerFinished.tryAcquire(1, kExportGateTimeoutMs));
    QTRY_COMPARE(saved.count(), 1);
    QVERIFY(!backend.canCancelExport());
    QCOMPARE(closed.count(), 1);
    QCOMPARE(discarded.count(), 0);
}

void tst_RecordingPreviewExport::previewWindowKeepsNativeCaption()
{
#ifdef Q_OS_WIN
    // Qt 6.11's Windows backend builds a plain Qt::Window that also carries
    // WindowStaysOnTopHint as a caption-less WS_POPUP frame unless the caption
    // hints are spelled out. The preview must keep its title bar and buttons.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.show();
    QWindow* view = nullptr;
    auto findView = [&view]() {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview")) {
                view = window;
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(findView(), 5000);
    QVERIFY(QTest::qWaitForWindowExposed(view));

    const HWND hwnd = reinterpret_cast<HWND>(view->winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const QString styles = QStringLiteral("style 0x%1 exstyle 0x%2").arg(qulonglong(style), 0, 16).arg(qulonglong(exStyle), 0, 16);
    QVERIFY2((style & WS_CAPTION) == WS_CAPTION, qPrintable(styles));
    QVERIFY2(style & WS_SYSMENU, qPrintable(styles));
    QVERIFY2(style & WS_MINIMIZEBOX, qPrintable(styles));
    QVERIFY2(style & WS_MAXIMIZEBOX, qPrintable(styles));
    QVERIFY2(style & WS_THICKFRAME, qPrintable(styles));
    QVERIFY2(exStyle & WS_EX_TOPMOST, qPrintable(styles));
    QVERIFY(view->frameMargins().top() > 0);
#else
    QSKIP("Native caption styles are checked on Windows only");
#endif
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
    else if (action == 2) {
        // An unedited MP4 save is decided on a worker; with no transcoder
        // available it saves the original as is.
        auto restore = qScopeGuard([]() { RecordingPreviewBackend::transcoderFactoryOverride() = {}; });
        RecordingPreviewBackend::transcoderFactoryOverride() = []() { return std::unique_ptr<IVideoTranscoder>(); };
        backend.save();
        QTRY_COMPARE(closed.count(), 1);
    } else backend.close();
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
