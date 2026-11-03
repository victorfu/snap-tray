#include <QtTest/QtTest>
#include <QThread>

#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "qml/RecordingPreviewBackend.h"
#include "recording/DiskSpaceGuard.h"
#include "recording/WindowTimeline.h"
#include "recording/WindowTimelineSidecar.h"
#include "recording/CursorTimeline.h"
#include "settings/RecordingSettingsManager.h"
#include "qml/RecordingCursorOverlay.h"
#include "qml/RecordingAppearanceOverlay.h"
#include "video/IVideoFrameReader.h"
#include <QPainter>
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

// An export ends either handed over for saving (the preview then keeps the
// source until completeSave()) or with the preview idle again.
#define WAIT_FOR_EXPORT(backend, savedSpy, timeoutMs) \
    QTRY_VERIFY_WITH_TIMEOUT((savedSpy).count() > 0 || !(backend).isProcessing(), timeoutMs)

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

// Pointer fixture (#160): a 10x10 magenta pointer, hotspot at its corner, at
// (40,30) until 500 ms, then at (100,70).
const QColor kPointerColor(255, 0, 255);
constexpr int kPointerSide = 10;

bool writePointerSidecar(const QString& videoPath, const QSize& frameSize)
{
    SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(videoPath));
    SnapTray::CursorTimelineInfo info;
    info.frameSize = frameSize;
    info.shapesAvailable = true;
    if (!writer.open(info)) return false;
    QImage shape(kPointerSide, kPointerSide, QImage::Format_ARGB32_Premultiplied);
    shape.fill(kPointerColor);
    writer.addShape({1, QPoint(0, 0), shape});
    writer.addSample({0, QPoint(40, 30), true, 1});
    writer.addSample({500, QPoint(100, 70), true, 1});
    return writer.finish();
}

// Top-left of the magenta block, or (-1,-1).
QPoint pointerTopLeft(const QImage& image)
{
    QRect found;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.red() > 170 && c.blue() > 170 && c.green() < 100) found |= QRect(x, y, 1, 1);
        }
    }
    return found.isEmpty() ? QPoint(-1, -1) : found.topLeft();
}

// The frame of an animated image shown at `ms`.
QImage animationFrameAt(const QString& path, qint64 ms)
{
    QImageReader reader(path);
    qint64 start = 0;
    QImage frame;
    while (reader.canRead()) {
        frame = reader.read();
        const int delay = reader.nextImageDelay();
        if (ms < start + qMax(delay, 1)) return frame;
        start += qMax(delay, 1);
    }
    return frame;
}

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

// The same, for a system whose MP4 export can change size and frame rate.
class OptionsGatedTranscoder : public GatedTranscoder
{
public:
    using GatedTranscoder::GatedTranscoder;
    bool supportsOutputOptions() const override { return true; }
};

} // namespace

class tst_RecordingPreviewExport : public QObject
{
    Q_OBJECT

private slots:
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
    void cancelAnimationKeepsSource_data();
    void cancelAnimationKeepsSource();
    void saveOutcomeKeepsRecordingUntilCommitted_data();
    void saveOutcomeKeepsRecordingUntilCommitted();
    void closeDuringExportAsks();
    void smartSaveWithoutRoomSavesAsRecorded();
    void savingOverTheSourceKeepsTheSavedFile();
    void pointerMatchesAcrossPreviewAndOutputs_data();
    void pointerMatchesAcrossPreviewAndOutputs();
    void pointerEffectsOnlyReencodeWhenDrawn();
    void advancedPointerEffectsMatchAcrossPreviewAndOutputs_data();
    void mp4OptionsReachTheOutput();
    void mp4OptionsMatchingTheSourceKeepTheFastPath();
    void sourceProbeNeverBlocksTheGuiThread();
    void saveBeforeTheProbeDecidesTheRoomAfterIt();
    void mp4OptionsTurnAnUneditedSaveIntoAnExport_data();
    void mp4OptionsTurnAnUneditedSaveIntoAnExport();
    void appearanceMatchesAcrossPreviewAndOutputs_data();
    void appearanceMatchesAcrossPreviewAndOutputs();
    void blurredBackgroundNeverShowsMaskedContent();
    void advancedPointerEffectsMatchAcrossPreviewAndOutputs();
    void pointerMovesOverAStillPicture();
    void zoomMatchesAcrossPreviewAndOutputs_data();
    void zoomMatchesAcrossPreviewAndOutputs();
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
    // Animated exports can be cancelled like MP4 (see cancelAnimationKeepsSource).
    QVERIFY(backend.canCancelExport());
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QCOMPARE(backend.selectedFormat(), format);
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    // Handed over for saving: the source stays until the save is committed.
    QVERIFY(backend.isProcessing());
    QVERIFY(!backend.canCancelExport());
    QVERIFY(QFileInfo::exists(inputPath));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
    QVERIFY(!backend.isProcessing());

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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(backend.processProgress(), 100);
    QVERIFY(QFileInfo::exists(inputPath));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);

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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    {
        std::lock_guard<std::mutex> lock(log->mutex);
        QCOMPARE(log->calls, 1);
        QCOMPARE(log->lastRequest.startMs, trimStartMs);
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
    QVERIFY(backend.canRetrySave());
    backend.clearError();
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(closedSpy.count(), 0);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QCOMPARE(closedSpy.count(), 0);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 60000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString outputPath = savedSpy.first().at(0).toString();
    QVERIFY(outputPath != inputPath);
    QVERIFY(QFileInfo::exists(inputPath));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), noiseInputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QVERIFY(QFileInfo::exists(noiseInputPath));
    QVERIFY(backend.errorMessage().isEmpty());
    {
        std::lock_guard<std::mutex> lock(log->mutex);
        QCOMPARE(log->calls, 0);
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);

    // PR #197 review: an explicit quality is honoured. The noise fixture is
    // above Low's target, so choosing Low re-encodes it.
    const QJsonObject rememberedOptions = RecordingSettingsManager::instance().mp4ExportOptions();
    auto restoreOptions = qScopeGuard([rememberedOptions]() {
        RecordingSettingsManager::instance().setMp4ExportOptions(rememberedOptions);
    });
    backend.setMp4Quality(int(SnapTray::Mp4ExportOptions::Quality::Low));
    // The estimate agrees with the export: less than the file (PR #197 review).
    QTRY_VERIFY(backend.sourceProbeReady());
    QVERIFY(backend.mp4EstimatedBytes() < QFileInfo(noiseInputPath).size());
    savedSpy.clear();
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QVERIFY(savedSpy.first().at(0).toString() != noiseInputPath);
    {
        std::lock_guard<std::mutex> lock(log->mutex);
        QCOMPARE(log->calls, 1);
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);
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
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
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
    // The preview closes once the save is committed, not before.
    QCOMPARE(closed.count(), 0);
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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

void tst_RecordingPreviewExport::cancelAnimationKeepsSource_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<bool>("afterWorker");
    QTest::newRow("gif-at-start") << int(RecordingPreviewBackend::GIF) << false;
    QTest::newRow("webp-at-start") << int(RecordingPreviewBackend::WebP) << false;
    QTest::newRow("gif-before-gui-commit") << int(RecordingPreviewBackend::GIF) << true;
    QTest::newRow("webp-before-gui-commit") << int(RecordingPreviewBackend::WebP) << true;
}

void tst_RecordingPreviewExport::cancelAnimationKeepsSource()
{
    QFETCH(int, format);
    QFETCH(bool, afterWorker);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(kFrameSize);
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = input.readAll();
    input.close();

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setTrimStart(kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QVERIFY(backend.isProcessing());
    QVERIFY(backend.canCancelExport());
    QVERIFY(!backend.processIndeterminate());

    if (afterWorker) {
        // Let the worker write and promote its output without running the
        // event loop: its completion is still queued when the cancel lands.
        const QString extension = format == RecordingPreviewBackend::GIF ? QStringLiteral("*.gif")
                                                                         : QStringLiteral("*.webp");
        QElapsedTimer timer;
        timer.start();
        while (QDir(directory.path()).entryList(QStringList(extension), QDir::Files).isEmpty()
               && timer.elapsed() < 20000) {
            QThread::msleep(kFakeCancelPollMs);
        }
        QVERIFY(!QDir(directory.path()).entryList(QStringList(extension), QDir::Files).isEmpty());
    }
    backend.cancelExport();
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Cancelling..."));
    QVERIFY(!backend.canCancelExport());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);

    // Quiet cancel: no error, nothing handed over, the window stays.
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QTRY_COMPARE(QDir(directory.path()).entryList(QDir::Files),
                 QStringList({QStringLiteral("recording.mp4"),
                              QFileInfo(SnapTray::WindowTimelineSidecar::pathFor(inputPath)).fileName()}));
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), originalBytes);
    input.close();
    // Edits survive, so the user can change them and try again right away.
    QCOMPARE(backend.trimStart(), qint64(kFrameIntervalMs));
    backend.save();
    QVERIFY(backend.canCancelExport());
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QCOMPARE(savedSpy.count(), 1);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
}

void tst_RecordingPreviewExport::saveOutcomeKeepsRecordingUntilCommitted_data()
{
    QTest::addColumn<int>("outcome");
    QTest::newRow("cancelled") << int(RecordingPreviewBackend::SaveOutcome::Cancelled);
    QTest::newRow("failed") << int(RecordingPreviewBackend::SaveOutcome::Failed);
    QTest::newRow("saved") << int(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::saveOutcomeKeepsRecordingUntilCommitted()
{
    QFETCH(int, outcome);
    const auto result = static_cast<RecordingPreviewBackend::SaveOutcome>(outcome);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(kFrameSize);
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setTrimStart(kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();
    // Waiting for the save dialog: indeterminate "Saving...", not cancellable,
    // source and sidecar still there.
    QVERIFY(backend.isProcessing());
    QVERIFY(backend.processIndeterminate());
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Saving..."));
    QVERIFY(!backend.canCancelExport());
    QVERIFY(QFileInfo::exists(inputPath));
    QVERIFY(QFileInfo::exists(SnapTray::WindowTimelineSidecar::pathFor(inputPath)));
    backend.save();  // ignored while waiting
    QCOMPARE(savedSpy.count(), 1);

    backend.completeSave(result);
    QVERIFY(!backend.isProcessing());
    if (result == RecordingPreviewBackend::SaveOutcome::Saved) {
        QCOMPARE(closedSpy.count(), 1);
        QVERIFY(!QFileInfo::exists(inputPath));
        QVERIFY(QFileInfo::exists(output));  // the consumer moves it to its destination
        return;
    }
    // Not saved: back to editing with the recording, its timeline and the trim.
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(QFileInfo::exists(inputPath));
    QVERIFY(QFileInfo::exists(SnapTray::WindowTimelineSidecar::pathFor(inputPath)));
    QVERIFY(!QFileInfo::exists(output));
    QCOMPARE(backend.trimStart(), qint64(kFrameIntervalMs));
    const bool failed = result == RecordingPreviewBackend::SaveOutcome::Failed;
    QCOMPARE(backend.errorMessage().isEmpty(), !failed);
    QCOMPARE(backend.canRetrySave(), failed);
    // A late or repeated outcome does nothing.
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
    QVERIFY(QFileInfo::exists(inputPath));
    QCOMPARE(closedSpy.count(), 0);
}

void tst_RecordingPreviewExport::closeDuringExportAsks()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.show();
    QWindow* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview"))
                view = window;
        }
        return view != nullptr;
    }(), 5000);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QVERIFY(backend.canCancelExport());

    // Closing during the export never closes or drops it: it asks.
    QVERIFY(!view->close());
    QVERIFY(backend.exportCloseQuestionPending());
    QVERIFY(backend.isProcessing());
    backend.keepExporting();
    QVERIFY(!backend.exportCloseQuestionPending());
    QVERIFY(backend.isProcessing());

    QVERIFY(!view->close());
    QVERIFY(backend.exportCloseQuestionPending());
    backend.cancelExportFromClose();
    QVERIFY(!backend.exportCloseQuestionPending());
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(QFileInfo::exists(inputPath));
}

void tst_RecordingPreviewExport::pointerMatchesAcrossPreviewAndOutputs_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("mp4") << int(RecordingPreviewBackend::MP4);
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::pointerMatchesAcrossPreviewAndOutputs()
{
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(20, 10, 120, 100);
    constexpr qint64 kTrimStart = 300;
    constexpr qint64 kTrimEnd = 900;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QVERIFY(backend.hasPointerData());
    if (!backend.cursorEffectsAvailable()) {
        QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
    }
    backend.setShowCursor(true);
    backend.setCursorScale(1.0);
    backend.setCursorSmoothing(0);
    QVERIFY(backend.cursorEffectsActive());
    backend.setCropRect(crop);
    backend.setTrimStart(kTrimStart);
    backend.setTrimEnd(kTrimEnd);

    // The preview overlay at a fixed source time (full frame, 1:1).
    const struct { qint64 sourceMs; QPoint sourcePoint; } checks[] = {
        {400, QPoint(40, 30)},
        {700, QPoint(100, 70)},
    };
    RecordingCursorOverlay overlay;
    overlay.setBackend(&backend);
    overlay.setContentRect(QRectF(QPointF(0, 0), QSizeF(sourceSize)));
    for (const auto& check : checks) {
        QImage preview(sourceSize, QImage::Format_RGB32);
        preview.fill(Qt::black);
        overlay.setPosition(check.sourceMs);
        QPainter painter(&preview);
        overlay.paint(&painter);
        painter.end();
        QCOMPARE(pointerTopLeft(preview), check.sourcePoint);
    }

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    // Changing the look while exporting must not reach the running job.
    backend.setCursorScale(3.0);
    QVERIFY(qFuzzyCompare(backend.cursorScale(), 1.0));
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();

    // The same source times in the output: geometry through the crop, time through the trim.
    for (const auto& check : checks) {
        const qint64 editedMs = check.sourceMs - kTrimStart;
        QImage frame;
        if (format == RecordingPreviewBackend::MP4) {
            auto reader = IVideoFrameReader::create();
            if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
            QVERIFY2(reader->load(output), qPrintable(reader->lastError()));
            frame = reader->frameAt(editedMs);
        } else {
            frame = animationFrameAt(output, editedMs);
        }
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.size(), crop.size());
        const QPoint expected = check.sourcePoint - crop.topLeft();
        const QPoint actual = pointerTopLeft(frame);
        // Codec and palette error may blur an edge by a pixel, never more.
        QVERIFY2((actual - expected).manhattanLength() <= 2,
                 qPrintable(QStringLiteral("at %1 ms: expected %2,%3 got %4,%5")
                                .arg(check.sourceMs).arg(expected.x()).arg(expected.y())
                                .arg(actual.x()).arg(actual.y())));
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::advancedPointerEffectsMatchAcrossPreviewAndOutputs_data()
{
    pointerMatchesAcrossPreviewAndOutputs_data();
}

void tst_RecordingPreviewExport::advancedPointerEffectsMatchAcrossPreviewAndOutputs()
{
    // #186: a hidden range and "still end" through crop and trim, the same in
    // the preview and in each output.
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(20, 10, 120, 100);
    constexpr qint64 kTrimStart = 300;
    constexpr qint64 kTrimEnd = 900;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (!backend.cursorEffectsAvailable()) {
        QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
    }
    backend.setShowCursor(true);
    backend.setCursorScale(1.0);
    backend.setCursorSmoothing(0);
    backend.setCursorIdleHide(0);
    backend.setCursorMotionBlur(0);
    backend.setCropRect(crop);
    backend.setTrimStart(kTrimStart);
    backend.setTrimEnd(kTrimEnd);
    // The look is remembered: leave it as the other tests expect it (directly:
    // the backend refuses edits while a save waits for its outcome).
    const QJsonObject rememberedLook = RecordingSettingsManager::instance().pointerEffects();
    auto restore = qScopeGuard([rememberedLook]() {
        RecordingSettingsManager::instance().setPointerEffects(rememberedLook);
    });
    // Held still from 400 ms (trim end 900 minus 500 ms): the move at 500 ms is not shown.
    backend.setCursorStillEnd(500);
    // Covers the source frames at 300 and 400 ms (frames are 100 ms apart).
    QCOMPARE(backend.addPointerHideAt(300), 0);
    QVERIFY(!backend.setPointerHideRange(0, 300, 380)); // 80 ms: shorter than the minimum
    QVERIFY(backend.setPointerHideRange(0, 290, 450));

    const QPoint none(-1, -1);
    const struct { qint64 sourceMs; QPoint sourcePoint; } checks[] = {
        {350, none},               // hidden range
        {600, QPoint(40, 30)},     // still end: where it was at 400 ms
        {850, QPoint(40, 30)},
    };
    RecordingCursorOverlay overlay;
    overlay.setBackend(&backend);
    overlay.setContentRect(QRectF(QPointF(0, 0), QSizeF(sourceSize)));
    for (const auto& check : checks) {
        QImage preview(sourceSize, QImage::Format_RGB32);
        preview.fill(Qt::black);
        overlay.setPosition(check.sourceMs);
        QPainter painter(&preview);
        overlay.paint(&painter);
        painter.end();
        QCOMPARE(pointerTopLeft(preview), check.sourcePoint);
    }

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();
    for (const auto& check : checks) {
        const qint64 editedMs = check.sourceMs - kTrimStart;
        QImage frame;
        if (format == RecordingPreviewBackend::MP4) {
            auto reader = IVideoFrameReader::create();
            if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
            QVERIFY2(reader->load(output), qPrintable(reader->lastError()));
            frame = reader->frameAt(editedMs);
        } else {
            frame = animationFrameAt(output, editedMs);
        }
        QVERIFY(!frame.isNull());
        const QPoint actual = pointerTopLeft(frame);
        if (check.sourcePoint == none) {
            QVERIFY2(actual == none, qPrintable(QStringLiteral("pointer drawn at %1 ms").arg(check.sourceMs)));
            continue;
        }
        const QPoint expected = check.sourcePoint - crop.topLeft();
        QVERIFY2((actual - expected).manhattanLength() <= 2,
                 qPrintable(QStringLiteral("at %1 ms: expected %2,%3 got %4,%5")
                                .arg(check.sourceMs).arg(expected.x()).arg(expected.y())
                                .arg(actual.x()).arg(actual.y())));
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::pointerEffectsOnlyReencodeWhenDrawn()
{
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));
    auto restore = qScopeGuard([]() { RecordingPreviewBackend::outputQualityOverride() = {}; });
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    // Pointer hidden: an unedited, small recording is still moved as is.
    {
        RecordingPreviewBackend backend(inputPath);
        backend.setSelectedFormat(RecordingPreviewBackend::MP4);
        backend.updateVideoSize(sourceSize);
        backend.updateDuration(kFrameCount * kFrameIntervalMs);
        backend.setShowCursor(false);
        backend.setClickRipple(false);
        QVERIFY(!backend.cursorEffectsActive());
        QVERIFY(!backend.hasEdits());
        QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
        backend.save();
        WAIT_FOR_EXPORT(backend, savedSpy, 20000);
        QCOMPARE(savedSpy.count(), 1);
        QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
        backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);
    }
    // Pointer shown: drawing it is an edit, so the file is re-encoded.
    {
        RecordingPreviewBackend backend(inputPath);
        backend.setSelectedFormat(RecordingPreviewBackend::MP4);
        backend.updateVideoSize(sourceSize);
        backend.updateDuration(kFrameCount * kFrameIntervalMs);
        if (!backend.cursorEffectsAvailable()) {
            QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
        }
        backend.setShowCursor(true);
        // A look, not a footage edit: undoable, but "Reset Crop and Trim" keeps it.
        QVERIFY(!backend.hasEdits());
        QVERIFY(backend.canUndo());
        QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
        backend.save();
        WAIT_FOR_EXPORT(backend, savedSpy, 20000);
        QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
        QCOMPARE(savedSpy.count(), 1);
        QVERIFY(savedSpy.first().at(0).toString() != inputPath);
        backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
    }
}

// #190: a smaller size, a lower frame rate and HEVC reach the MP4, with the
// pointer and a mask drawn where they belong in the smaller picture.
void tst_RecordingPreviewExport::mp4OptionsReachTheOutput()
{
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));
    const QJsonObject rememberedLook = RecordingSettingsManager::instance().pointerEffects();
    const QJsonObject rememberedOptions = RecordingSettingsManager::instance().mp4ExportOptions();
    auto restore = qScopeGuard([rememberedLook, rememberedOptions]() {
        RecordingSettingsManager::instance().setPointerEffects(rememberedLook);
        RecordingSettingsManager::instance().setMp4ExportOptions(rememberedOptions);
    });

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QTRY_VERIFY(backend.sourceProbeReady()); // probed off the GUI thread
    if (!backend.mp4OutputOptionsAvailable() || !backend.cursorEffectsAvailable()) {
        QSKIP("This platform's MP4 transcoder cannot change size and frame rate yet");
    }
    backend.setShowCursor(true);
    backend.setCursorScale(1.0);
    backend.setCursorSmoothing(0);
    // A solid mask over the top-left 40x40 of the picture.
    QCOMPARE(backend.addMask(), 0);
    backend.setMaskRectFromView(0, QRectF(0, 0, 40, 40), QRectF(QPointF(0, 0), QSizeF(sourceSize)));
    backend.setMp4Resolution(int(SnapTray::Mp4ExportOptions::Resolution::CustomWidth));
    backend.setMp4CustomWidth(80);
    backend.setMp4FrameRate(15); // above the source's 10: no change
    QVERIFY(backend.mp4OptionsActive()); // the size still changes
    QCOMPARE(backend.mp4OutputFrameRate(), kFrameRate);
    backend.setMp4FrameRate(0);
    const bool hevc = backend.mp4HevcAvailable();
    if (hevc) backend.setMp4Codec(int(SnapTray::Mp4ExportOptions::Codec::Hevc));
    QCOMPARE(backend.mp4OutputSize(), QSize(80, 60));
    QVERIFY(backend.mp4EstimatedBytes() > 0);
    QVERIFY(!backend.mp4EstimateText().isEmpty());

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    // Options changed during the export do not reach it.
    backend.setMp4CustomWidth(40);
    QCOMPARE(backend.mp4CustomWidth(), 80);
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize(80, 60)); // the file name's size

    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(output);
    QCOMPARE(probe.videoSize, QSize(80, 60));
    QCOMPARE(probe.videoCodec, QString::fromLatin1(hevc ? kVideoCodecHevc : kVideoCodecH264));
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
    QVERIFY2(reader->load(output), qPrintable(reader->lastError()));
    const QImage frame = reader->frameAt(700);
    QVERIFY(!frame.isNull());
    // The pointer, at (100,70) from 500 ms, is at half its position and size.
    const QPoint pointer = pointerTopLeft(frame);
    QVERIFY2((pointer - QPoint(50, 35)).manhattanLength() <= 2,
             qPrintable(QStringLiteral("pointer at %1,%2").arg(pointer.x()).arg(pointer.y())));
    // The mask covers the top-left 20x20 of the smaller picture.
    const QColor masked = frame.pixelColor(10, 10);
    QVERIFY2(masked.lightness() < 40, qPrintable(masked.name()));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

// Options that change nothing for this recording (no smaller size, no lower
// frame rate, H.264) keep an unedited recording on the move-as-is path.
// PR #197 review: the MP4 options and estimate read the source's probe, which
// can scan a whole file (Windows reads every audio packet). It runs off the
// GUI thread: the getters answer at once and update when the probe is ready.
void tst_RecordingPreviewExport::sourceProbeNeverBlocksTheGuiThread()
{
    auto gate = std::make_shared<ExportGate>();
    gate->blockProbe = true;
    auto restore = qScopeGuard([gate]() {
        gate->resumeProbe.release();
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
    });
    RecordingPreviewBackend::transcoderFactoryOverride() = [gate]() -> std::unique_ptr<IVideoTranscoder> {
        return std::make_unique<GatedTranscoder>(gate);
    };
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QSignalSpy estimate(&backend, &RecordingPreviewBackend::mp4EstimateChanged);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!backend.sourceProbeReady());
    (void)backend.mp4OptionsActive();
    (void)backend.mp4EstimatedBytes();
    (void)backend.mp4SourceFrameRate();
    QVERIFY(timer.elapsed() < 1000); // the probe is still held at the gate
    QVERIFY(gate->probeEntered.tryAcquire(1, kExportGateTimeoutMs));
    QVERIFY(!backend.sourceProbeReady());
    gate->resumeProbe.release();
    QTRY_VERIFY(backend.sourceProbeReady());
    QVERIFY(estimate.count() >= 1);
    QCOMPARE(backend.mp4SourceFrameRate(), 10);
    QVERIFY(!gate->timedOut.load());
}

// PR #197 review: Save pressed while the source is still being probed, with
// almost no room. Options that change nothing are still a move; options that
// do change the stream are refused for space, once the job has probed.
void tst_RecordingPreviewExport::saveBeforeTheProbeDecidesTheRoomAfterIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    {
        QFile file(inputPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'v'));
    }
    const QJsonObject rememberedOptions = RecordingSettingsManager::instance().mp4ExportOptions();
    auto gate = std::make_shared<ExportGate>();
    auto restore = qScopeGuard([gate, rememberedOptions]() {
        gate->resumeProbe.release();
        RecordingSettingsManager::instance().setMp4ExportOptions(rememberedOptions);
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
        RecordingPreviewBackend::freeBytesForPathOverride() = {};
    });
    RecordingPreviewBackend::transcoderFactoryOverride() = [gate]() -> std::unique_ptr<IVideoTranscoder> {
        return std::make_unique<OptionsGatedTranscoder>(gate);
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };
    RecordingPreviewBackend::freeBytesForPathOverride() = [](const QString&) { return qint64(1); };

    for (const bool optionsChangeTheStream : {false, true}) {
        // The preview's own probe (the first) is held: Save cannot know yet.
        gate->firstProbe.store(true);
        gate->blockProbe = true;
        RecordingPreviewBackend backend(inputPath);
        backend.setSelectedFormat(RecordingPreviewBackend::MP4);
        backend.updateVideoSize(QSize(160, 120));
        backend.updateDuration(1000);
        if (optionsChangeTheStream) {
            backend.setMp4Resolution(int(SnapTray::Mp4ExportOptions::Resolution::CustomWidth));
            backend.setMp4CustomWidth(80); // smaller than the 160 px source
        } else {
            backend.setMp4Resolution(int(SnapTray::Mp4ExportOptions::Resolution::P720)); // larger: no change
            backend.setMp4FrameRate(60);                                                  // above its 10 fps
        }
        QVERIFY(backend.mp4OutputOptionsAvailable());
        QVERIFY(!backend.sourceProbeReady());
        QVERIFY(gate->probeEntered.tryAcquire(1, kExportGateTimeoutMs));
        QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
        backend.save();
        QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage())); // not refused up front
        if (optionsChangeTheStream) {
            QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 10000);
            QCOMPARE(savedSpy.count(), 0);
            QVERIFY(backend.errorMessage().contains(QStringLiteral("Not enough free disk space")));
            QVERIFY(backend.canRetrySave());
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(savedSpy.count(), 1, 10000); // handed to the save dialog
            QCOMPARE(savedSpy.first().at(0).toString(), inputPath); // moved as recorded
            QVERIFY(backend.errorMessage().isEmpty());
            backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);
        }
        QVERIFY(QFileInfo::exists(inputPath));
        gate->resumeProbe.release();
        QTRY_VERIFY(backend.sourceProbeReady());
    }
}

void tst_RecordingPreviewExport::mp4OptionsMatchingTheSourceKeepTheFastPath()
{
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    const QJsonObject rememberedOptions = RecordingSettingsManager::instance().mp4ExportOptions();
    auto restore = qScopeGuard([rememberedOptions]() {
        RecordingSettingsManager::instance().setMp4ExportOptions(rememberedOptions);
        RecordingPreviewBackend::outputQualityOverride() = {};
        RecordingPreviewBackend::freeBytesForPathOverride() = {};
    });
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };
    // Almost no room: a move needs none, so options that change nothing must
    // not be refused for space (PR #197 review).
    RecordingPreviewBackend::freeBytesForPathOverride() = [](const QString&) { return qint64(1); };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setMp4Resolution(int(SnapTray::Mp4ExportOptions::Resolution::P720)); // larger than the source
    backend.setMp4FrameRate(60);                                                  // above the source's rate
    QTRY_VERIFY(backend.sourceProbeReady());
    QVERIFY(!backend.mp4OptionsActive());
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(backend.mp4EstimatedBytes(), QFileInfo(inputPath).size());
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Cancelled);
}

void tst_RecordingPreviewExport::mp4OptionsTurnAnUneditedSaveIntoAnExport_data()
{
    QTest::addColumn<bool>("recordedAsIntermediate");
    QTest::newRow("intermediate") << true;
    QTest::newRow("recorded at the selected quality") << false;
}

// Unedited, but a smaller size is asked for: an export, never a move, on both
// smart-save routes (the probe decides, off the GUI thread).
void tst_RecordingPreviewExport::mp4OptionsTurnAnUneditedSaveIntoAnExport()
{
    QFETCH(bool, recordedAsIntermediate);
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    const QJsonObject rememberedOptions = RecordingSettingsManager::instance().mp4ExportOptions();
    auto restore = qScopeGuard([rememberedOptions]() {
        RecordingSettingsManager::instance().setMp4ExportOptions(rememberedOptions);
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    // Any recording meets quality 0: without the options it would be moved.
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath, recordedAsIntermediate);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (!backend.mp4OutputOptionsAvailable()) QSKIP("This platform's MP4 transcoder cannot change the size yet");
    backend.setMp4Resolution(int(SnapTray::Mp4ExportOptions::Resolution::CustomWidth));
    backend.setMp4CustomWidth(80);
    QVERIFY(!backend.hasEdits());
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();
    QVERIFY(output != inputPath);
    QCOMPARE(IVideoTranscoder::create()->probe(output).videoSize, QSize(80, 60));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::appearanceMatchesAcrossPreviewAndOutputs_data()
{
    pointerMatchesAcrossPreviewAndOutputs_data();
}

// #184: a white frame around a cropped picture with the pointer, the same in
// the preview and in each output.
void tst_RecordingPreviewExport::appearanceMatchesAcrossPreviewAndOutputs()
{
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(20, 10, 120, 100);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));
    const QJsonObject rememberedLook = RecordingSettingsManager::instance().pointerEffects();
    const QJsonObject rememberedFrame = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([rememberedLook, rememberedFrame]() {
        RecordingSettingsManager::instance().setPointerEffects(rememberedLook);
        RecordingSettingsManager::instance().setAppearance(rememberedFrame);
    });

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (!backend.appearanceAvailable() || !backend.cursorEffectsAvailable()) {
        QSKIP("This platform's MP4 transcoder cannot draw onto frames yet");
    }
    backend.setShowCursor(true);
    backend.setCursorScale(1.0);
    backend.setCursorSmoothing(0);
    backend.setCropRect(crop);
    backend.setAppearanceSolidColor(Qt::white); // also switches the frame on
    backend.setAppearancePadding(int(SnapTray::RecordingAppearance::Amount::Medium)); // 8 % of 100 px
    backend.setAppearanceCorners(int(SnapTray::RecordingAppearance::Amount::None));
    backend.setAppearanceShadow(false);
    backend.setAppearanceBorder(false);
    QVERIFY(backend.appearanceActive());
    QVERIFY(backend.effectsActive());
    const QSize framed(136, 116);
    const QPoint inset(8, 8);

    // The preview: the canvas around the crop as shown 1:1, framed in white,
    // with the picture left clear.
    const QRectF cropInView(crop);
    const QRectF canvas = backend.appearanceCanvasInView(cropInView);
    QCOMPARE(canvas, QRectF(QPointF(crop.topLeft() - inset), QSizeF(framed)));
    RecordingAppearanceOverlay overlay;
    overlay.setBackend(&backend);
    overlay.setContentRect(QRectF(QPointF(inset), QSizeF(crop.size())));
    QImage preview(framed, QImage::Format_ARGB32_Premultiplied);
    preview.fill(Qt::transparent);
    {
        QPainter painter(&preview);
        overlay.paint(&painter);
    }
    QCOMPARE(preview.pixelColor(2, 2), QColor(Qt::white));
    QCOMPARE(qAlpha(preview.pixel(inset + QPoint(60, 50))), 0);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    // Changing the frame while exporting must not reach the running job.
    backend.setAppearancePadding(int(SnapTray::RecordingAppearance::Amount::Large));
    QCOMPARE(backend.appearancePadding(), int(SnapTray::RecordingAppearance::Amount::Medium));
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(1).toSize(), framed);
    const QString output = savedSpy.first().at(0).toString();
    QImage frame;
    constexpr qint64 kAtMs = 200; // a red source frame; the pointer at (40,30)
    if (format == RecordingPreviewBackend::MP4) {
        auto reader = IVideoFrameReader::create();
        if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
        QVERIFY2(reader->load(output), qPrintable(reader->lastError()));
        frame = reader->frameAt(kAtMs);
    } else {
        frame = animationFrameAt(output, kAtMs);
    }
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.size(), framed);
    const QColor background = frame.pixelColor(2, 2);
    QVERIFY2(background.lightness() > 230, qPrintable(background.name()));
    const QColor picture = frame.pixelColor(inset + QPoint(100, 80));
    QVERIFY2(picture.red() > 170 && picture.green() < 90, qPrintable(picture.name()));
    // The pointer lands where it does in the preview: through the crop, then the inset.
    const QPoint pointer = pointerTopLeft(frame);
    const QPoint expected = QPoint(40, 30) - crop.topLeft() + inset;
    QVERIFY2((pointer - expected).manhattanLength() <= 2,
             qPrintable(QStringLiteral("pointer at %1,%2").arg(pointer.x()).arg(pointer.y())));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

// #184: a blurred-video background is made from the masked picture, so a
// mask over everything leaves no trace of the content around it either.
void tst_RecordingPreviewExport::blurredBackgroundNeverShowsMaskedContent()
{
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    const QJsonObject rememberedFrame = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([rememberedFrame]() {
        RecordingSettingsManager::instance().setAppearance(rememberedFrame);
    });

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QCOMPARE(backend.addMask(), 0);
    backend.setMaskRectFromView(0, QRectF(QPointF(0, 0), QSizeF(sourceSize)), QRectF(QPointF(0, 0), QSizeF(sourceSize)));
    backend.setAppearanceBackground(int(SnapTray::RecordingAppearance::Background::BlurredVideo));
    backend.setAppearancePadding(int(SnapTray::RecordingAppearance::Amount::Large));
    QVERIFY(backend.appearanceActive());

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QImage frame = animationFrameAt(savedSpy.first().at(0).toString(), 200); // red underneath
    QVERIFY(!frame.isNull());
    for (const QPoint at : {QPoint(2, 2), QPoint(frame.width() - 3, frame.height() / 2), QPoint(frame.width() / 2, 2)}) {
        const QColor c = frame.pixelColor(at);
        QVERIFY2(c.red() < 80, qPrintable(QStringLiteral("%1 at %2,%3").arg(c.name()).arg(at.x()).arg(at.y())));
    }
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::pointerMovesOverAStillPicture()
{
    // Source frames at 0-300 ms, then nothing until 1000 ms (a static screen
    // with a variable-frame-rate source); the pointer moves at 600 ms.
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    {
        std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
        QVERIFY(encoder);
        QVERIFY(encoder->start(inputPath, sourceSize, kFrameRate));
        QImage frame(sourceSize, QImage::Format_ARGB32);
        frame.fill(Qt::green);
        for (const qint64 ms : {0, 100, 200, 300, 1000, 1100}) {
            const qint64 before = encoder->framesWritten();
            QElapsedTimer wait;
            wait.start();
            do {
                encoder->writeFrame(frame, ms);
                if (encoder->framesWritten() != before) break;
                QTest::qWait(5);
            } while (wait.elapsed() < 2000);
        }
        QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
        encoder->finish();
        QVERIFY(!finished.isEmpty() || finished.wait(10000));
    }
    {
        SnapTray::CursorTimelineWriter writer(SnapTray::CursorTimelineSidecar::pathFor(inputPath));
        SnapTray::CursorTimelineInfo info;
        info.frameSize = sourceSize;
        info.shapesAvailable = true;
        QVERIFY(writer.open(info));
        QImage shape(kPointerSide, kPointerSide, QImage::Format_ARGB32_Premultiplied);
        shape.fill(kPointerColor);
        writer.addShape({1, QPoint(0, 0), shape});
        writer.addSample({0, QPoint(20, 20), true, 1});
        writer.addSample({600, QPoint(110, 80), true, 1});
        QVERIFY(writer.finish());
    }
    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(1200);
    if (!backend.cursorEffectsAvailable()) {
        QSKIP("This platform's MP4 transcoder cannot draw pointer effects yet");
    }
    backend.setShowCursor(true);
    backend.setCursorSmoothing(0);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
    QVERIFY(reader->load(savedSpy.first().at(0).toString()));
    const QPoint early = pointerTopLeft(reader->frameAt(250));
    const QPoint moved = pointerTopLeft(reader->frameAt(800));
    QVERIFY2((early - QPoint(20, 20)).manhattanLength() <= 2, qPrintable(QString::number(early.x())));
    // Without filling the gap, 800 ms would still show the 300 ms frame.
    QVERIFY2((moved - QPoint(110, 80)).manhattanLength() <= 2,
             qPrintable(QStringLiteral("%1,%2").arg(moved.x()).arg(moved.y())));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::zoomMatchesAcrossPreviewAndOutputs_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("mp4") << int(RecordingPreviewBackend::MP4);
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::zoomMatchesAcrossPreviewAndOutputs()
{
    // The pointer is the marker: zoomed 2x through a crop and trim, it must
    // land at the same place in the preview's viewport and in every output.
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(20, 10, 120, 100);
    constexpr qint64 kTrimStart = 300;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    QVERIFY(writePointerSidecar(inputPath, sourceSize));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (!backend.zoomAvailable()) QSKIP("This platform's MP4 transcoder cannot draw zooms yet");
    backend.setShowCursor(true);
    backend.setCursorScale(1.0);
    backend.setCursorSmoothing(0);
    backend.setClickRipple(false);
    backend.setCropRect(crop);
    backend.setTrimStart(kTrimStart);
    backend.setTrimEnd(900);
    const int index = backend.addZoomAt(350);
    QCOMPARE(index, 0);
    QVERIFY(backend.setZoomRange(0, 350, 850));
    backend.setZoomScale(0, 2.0);
    backend.setZoomFocusFromView(0, QPointF(80, 60), QRectF(QPointF(0, 0), QSizeF(sourceSize)));

    // Preview: a 60x50 view centred on (80,60), inside the crop.
    const QRectF contentRect(QPointF(0, 0), QSizeF(sourceSize));
    QCOMPARE(backend.zoomViewportInView(700, contentRect), QRectF(50, 35, 60, 50));
    // After it has eased out (850 + 600 ms): the whole crop again.
    QCOMPARE(backend.zoomViewportInView(1500, contentRect), QRectF(crop));

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 30000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString output = savedSpy.first().at(0).toString();
    QImage frame;
    const qint64 editedMs = 700 - kTrimStart;
    if (format == RecordingPreviewBackend::MP4) {
        auto reader = IVideoFrameReader::create();
        if (!reader) QSKIP("No frame reader to inspect the MP4 on this platform");
        QVERIFY(reader->load(output));
        frame = reader->frameAt(editedMs);
    } else {
        frame = animationFrameAt(output, editedMs);
    }
    QCOMPARE(frame.size(), crop.size());
    // Source (100,70) in view (50,35,60,50) at 2x: output (100,70); the 10 px pointer is 20 px.
    const QPoint actual = pointerTopLeft(frame);
    QVERIFY2((actual - QPoint(100, 70)).manhattanLength() <= 3,
             qPrintable(QStringLiteral("%1,%2").arg(actual.x()).arg(actual.y())));
    int magenta = 0;
    for (int y = 0; y < frame.height(); ++y)
        for (int x = 0; x < frame.width(); ++x) {
            const QColor c = frame.pixelColor(x, y);
            magenta += c.red() > 170 && c.blue() > 170 && c.green() < 100 ? 1 : 0;
        }
    QVERIFY2(magenta > 250, qPrintable(QString::number(magenta))); // ~20x20, not 10x10: zoomed
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
}

void tst_RecordingPreviewExport::savingOverTheSourceKeepsTheSavedFile()
{
    // PR #168 review: choosing the recording's own path in the save dialog
    // replaced it with the export, and then the source clean-up deleted it.
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    backend.setTrimStart(200);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 20000);
    QCOMPARE(savedSpy.count(), 1);
    const QString exported = savedSpy.first().at(0).toString();
    QVERIFY(exported != inputPath);
    // What the save dialog does when the user picks the recording's path.
    QVERIFY(QFile::remove(inputPath));
    QVERIFY(QFile::rename(exported, inputPath));
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved, inputPath);
    QVERIFY2(QFileInfo::exists(inputPath), "the saved recording was deleted");
    QVERIFY(QFileInfo(inputPath).size() > 0);
}

void tst_RecordingPreviewExport::smartSaveWithoutRoomSavesAsRecorded()
{
    // PR #171 review: an unedited recording above the quality target is
    // re-encoded into a second file, which skipped the free-space check.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createNoiseRecording(inputPath);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));
    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
        RecordingPreviewBackend::freeBytesForPathOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };
    // Just past the floor: not enough for a second copy of the recording.
    RecordingPreviewBackend::freeBytesForPathOverride() = [](const QString&) {
        return SnapTray::DiskSpaceGuard::kFloorBytes + 1024;
    };
    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(kNoiseSize);
    backend.updateDuration(kNoiseFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    WAIT_FOR_EXPORT(backend, savedSpy, 60000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    // Saved as recorded, without starting an export.
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QVERIFY(!log->started.load());
    backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(closed.count(), 0);  // waits for the save outcome
        backend.completeSave(RecordingPreviewBackend::SaveOutcome::Saved);
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
