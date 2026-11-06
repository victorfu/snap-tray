#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "VideoTranscoderTestAudio.h"
#include "video/Mp4VideoSamples.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"
#include "video/VideoTranscoderFaultInjection.h"
#include "recording/Mp4ExportOptions.h"
#include "recording/AudioTracks.h"
#include "encoding/VideoBitrate.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
#include "NativeEditTestMedia_linux.h"
#endif

namespace {
std::unique_ptr<IVideoTranscoder> createTestTranscoder()
{
#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
    if (qEnvironmentVariableIntValue("SNAPTRAY_TEST_NATIVE_EDIT_EXPORT") == 1) {
        qInfo() << "Testing the shared native edit exporter with FFmpeg codecs";
        return std::make_unique<NativeEditTestTranscoder>();
    }
#endif
    return IVideoTranscoder::create();
}

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 20;                  // 2 s
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerSample = 2;
constexpr int kAudioFramesPerVideoFrame = kAudioSampleRate / kFrameRate;
constexpr qint64 kDurationToleranceMs = 150;
constexpr double kFrameRateTolerance = 0.5; // nominal rates are exact on both platforms
constexpr double kMsPerSecond = 1000.0;
const QSize kSourceSize(160, 120);

// Audio fixture: 1 kHz tone bursts every 200 ms from 200 ms to 1800 ms. Each
// burst starts on a video frame that shows a black bottom-right marker, so
// output audio can be checked against the output video timeline.
constexpr int kPulseFirstMs = 200;
constexpr int kPulseSpacingMs = 200;
constexpr int kPulseCount = 9;
constexpr int kPulseDurationMs = 40;
constexpr double kPulseFrequencyHz = 1000.0;
constexpr double kPulseAmplitude = 0.5;          // of full scale
constexpr float kPulseDetectThreshold = 0.25f;   // half the burst amplitude
constexpr int kInt16Max = 32767;

// AAC packs 1024 samples per packet: 21.3 ms at 48 kHz. Passthrough copies
// whole packets, so timestamp mistakes show up in packet-sized steps: a lost
// or duplicated packet shifts audio by 21.3 ms, ignoring the AAC priming
// (2112 samples = 44 ms) shifts it by 44 ms. Correct retiming keeps the burst
// onset sample-accurate (onset detection itself is < 1 ms at this threshold),
// so half a packet both passes correct output and fails every packet-sized
// error.
constexpr int kAacPacketFrames = 1024;
constexpr double kAacPacketMs = kAacPacketFrames * kMsPerSecond / kAudioSampleRate;
constexpr double kPulseAlignmentToleranceMs = kAacPacketMs / 2;
// The fixture's own bursts are measured against their nominal schedule with
// the same half-packet bound; larger errors mean the encoder dropped audio.
constexpr double kFixturePulseToleranceMs = kAacPacketMs / 2;
// The edit list trims passthrough audio to the session, so the decoded audio
// must start at 0 and reach the end of the interval to within one packet.
constexpr double kAudioEdgeToleranceMs = kAacPacketMs;
// Video marker onsets are found by sampling frames every kVideoScanStepMs.
constexpr int kVideoScanStepMs = 5;
constexpr double kAvSyncToleranceMs = kPulseAlignmentToleranceMs + kVideoScanStepMs;

// 730 ms = 35040 samples: 224 samples into a 1024-sample packet on the plain
// grid and 288 samples in once the 2112-sample AAC priming offset is applied.
constexpr qint64 kUnalignedStartMs = 730;
constexpr qint64 kUnalignedEndMs = 1730;
static_assert((kUnalignedStartMs * kAudioSampleRate / 1000) % kAacPacketFrames != 0,
              "trim start must fall inside an AAC packet");

// Early-ending audio fixture: audio only for the first 800 ms of the 2 s
// video (bursts at 200, 400 and 600 ms), as when audio capture stops early.
constexpr int kEarlyAudioFrameCount = 8;
constexpr qint64 kEarlyAudioEndMs = kEarlyAudioFrameCount * kFrameIntervalMs;
constexpr qint64 kEarlyAudioMinGapMs = 1000;
static_assert(kFrameCount * kFrameIntervalMs - kEarlyAudioEndMs >= kEarlyAudioMinGapMs,
              "the audio must end at least 1 s before the video");
// probe() reads the audio range off packet/track timing: the AAC encoder may
// pad the end to a whole packet and shift the start by its priming.
constexpr qint64 kAudioRangeToleranceMs = 3 * kAacPacketFrames * 1000 / kAudioSampleRate; // 64 ms
// A transcode of the 2 s fixture takes well under a second; this bound only
// turns a hang into a test failure, inside the ctest TIMEOUT for the suite.
constexpr int kTranscodeHangTimeoutMs = 20000;
constexpr int kTranscodeHangPollMs = 10;

constexpr int kCancelAtPercent = 30;
constexpr int kMarkerDarkMax = 64;
constexpr int kMarkerLightMin = 192;

bool isPulseFrame(int frameIndex)
{
    const int timeMs = frameIndex * kFrameIntervalMs;
    return timeMs >= kPulseFirstMs && (timeMs - kPulseFirstMs) % kPulseSpacingMs == 0
        && (timeMs - kPulseFirstMs) / kPulseSpacingMs < kPulseCount;
}

// Quadrants: top-left red, top-right green, bottom-left blue, bottom-right
// white (black on a burst frame).
QImage fixtureFrame(int frameIndex)
{
    QImage frame(kSourceSize, QImage::Format_ARGB32);
    QPainter painter(&frame);
    const int halfW = kSourceSize.width() / 2;
    const int halfH = kSourceSize.height() / 2;
    painter.fillRect(0, 0, halfW, halfH, Qt::red);
    painter.fillRect(halfW, 0, halfW, halfH, Qt::green);
    painter.fillRect(0, halfH, halfW, halfH, Qt::blue);
    painter.fillRect(halfW, halfH, halfW, halfH, isPulseFrame(frameIndex) ? Qt::black : Qt::white);
    return frame;
}

// Interleaved 16-bit PCM for one video frame's worth of audio.
QByteArray fixtureAudio(int frameIndex)
{
    QByteArray pcm(kAudioFramesPerVideoFrame * kAudioChannels * kAudioBytesPerSample, '\0');
    auto* samples = reinterpret_cast<qint16*>(pcm.data());
    const qint64 firstFrame = qint64(frameIndex) * kAudioFramesPerVideoFrame;
    const qint64 pulseFrames = qint64(kPulseDurationMs) * kAudioSampleRate / 1000;
    for (int i = 0; i < kAudioFramesPerVideoFrame; ++i) {
        const qint64 frame = firstFrame + i;
        qint16 value = 0;
        for (int pulse = 0; pulse < kPulseCount; ++pulse) {
            const qint64 pulseStart = qint64(kPulseFirstMs + pulse * kPulseSpacingMs) * kAudioSampleRate / 1000;
            if (frame >= pulseStart && frame < pulseStart + pulseFrames) {
                const double t = static_cast<double>(frame - pulseStart) / kAudioSampleRate;
                value = static_cast<qint16>(std::lround(
                    kPulseAmplitude * kInt16Max * std::sin(2.0 * M_PI * kPulseFrequencyHz * t)));
            }
        }
        for (int channel = 0; channel < kAudioChannels; ++channel) {
            samples[i * kAudioChannels + channel] = value;
        }
    }
    return pcm;
}

// `audioFrameCount` video frames (from the start) get audio; the rest are
// video only.
QString createFixture(const QString& path, bool withAudio, int audioFrameCount = kFrameCount)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) {
        return QStringLiteral("No native encoder");
    }
    if (withAudio) {
        encoder->setAudioFormat(kAudioSampleRate, kAudioChannels, kAudioBytesPerSample * 8);
    }
    if (!encoder->start(path, kSourceSize, kFrameRate)) {
        return encoder->lastError();
    }
    if (withAudio && !encoder->isAudioEnabled()) {
        encoder->abort();
        return QStringLiteral("SKIP: AAC encoding unavailable");
    }
    for (int i = 0; i < kFrameCount; ++i) {
        const QImage frame = fixtureFrame(i);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer waitTimer;
        waitTimer.start();
        do {
            encoder->writeFrame(frame, i * kFrameIntervalMs);
            if (encoder->framesWritten() != before) {
                break;
            }
            QTest::qWait(5);
        } while (waitTimer.elapsed() < 2000);
        if (encoder->framesWritten() != before + 1) {
            return QStringLiteral("Encoder rejected fixture frame %1").arg(i);
        }
        if (withAudio && i < audioFrameCount) {
            encoder->writeAudioSamples(fixtureAudio(i), qint64(i) * kAudioFramesPerVideoFrame);
        }
    }
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) {
        return QStringLiteral("Encoder did not finish");
    }
    return finishedSpy.first().at(0).toBool() ? QString() : encoder->lastError();
}

bool isGreen(const QColor& c) { return c.green() > 180 && c.red() < 60 && c.blue() < 60; }

QByteArray fileHash(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result();
}

// Burst onsets in ms on the file's presentation timeline.
std::vector<double> pulseOnsetsMs(const DecodedAudio& audio)
{
    std::vector<double> onsets;
    const qint64 minSpacingFrames = qint64(kPulseSpacingMs / 2) * audio.sampleRate / 1000;
    qint64 nextAllowed = 0;
    for (qint64 i = 0; i < static_cast<qint64>(audio.mono.size()); ++i) {
        if (i >= nextAllowed && std::fabs(audio.mono[static_cast<size_t>(i)]) > kPulseDetectThreshold) {
            onsets.push_back((audio.firstFrame + i) * kMsPerSecond / audio.sampleRate);
            nextAllowed = i + minSpacingFrames;
        }
    }
    return onsets;
}

QString describe(const std::vector<double>& values)
{
    QStringList parts;
    for (double value : values) {
        parts << QString::number(value, 'f', 2);
    }
    return parts.join(QLatin1String(", "));
}

// Times at which the bottom-right burst marker turns black in `path`, found by
// sampling frames every kVideoScanStepMs (Windows: at every decoded frame's
// own timestamp). `markerPoint` is in output pixels.
std::vector<double> markerOnsetsMs(const QString& path, const QPoint& markerPoint, qint64 durationMs,
                                   QString* error)
{
    std::vector<double> onsets;
    bool dark = false;
    const auto addSample = [&](double timeMs, int lightness) {
        if (!dark && lightness <= kMarkerDarkMax) {
            onsets.push_back(timeMs);
            dark = true;
        } else if (dark && lightness >= kMarkerLightMin) {
            dark = false;
        }
    };
    auto reader = IVideoFrameReader::create();
#ifdef Q_OS_WIN
    if (!reader) {
        std::vector<DecodedVideoPixel> pixels;
        if (!decodeVideoPixels(path, markerPoint, &pixels, error)) {
            return onsets;
        }
        for (const DecodedVideoPixel& pixel : pixels) {
            if (pixel.timeMs < durationMs) {
                addSample(pixel.timeMs, QColor(pixel.color).lightness());
            }
        }
        return onsets;
    }
#endif
    if (!reader || !reader->load(path)) {
        *error = QStringLiteral("Cannot load output video");
        return onsets;
    }
    for (qint64 t = 0; t < durationMs; t += kVideoScanStepMs) {
        const QImage frame = reader->frameAt(t);
        if (frame.isNull()) {
            *error = reader->lastError();
            return onsets;
        }
        addSample(static_cast<double>(t), frame.pixelColor(markerPoint).lightness());
    }
    return onsets;
}

// Runs transcode() on its own thread with a fresh transcoder and waits at most
// kTranscodeHangTimeoutMs, so a pipeline that never returns fails the test
// instead of wedging the suite. A hung worker is detached; it owns everything
// it touches. Returns false on timeout.
bool transcodeWithHangGuard(const VideoTranscodeRequest& request, VideoTranscodeResult* result)
{
    struct Job {
        std::unique_ptr<IVideoTranscoder> transcoder = createTestTranscoder();
        VideoTranscodeRequest request;
        VideoTranscodeResult result;
        std::atomic<bool> done{false};
    };
    auto job = std::make_shared<Job>();
    job->request = request;
    if (!job->transcoder) {
        result->errorMessage = QStringLiteral("No native transcoder");
        return true;
    }
    std::thread worker([job]() {
        job->result = job->transcoder->transcode(job->request, {});
        job->done.store(true);
    });
    QElapsedTimer timer;
    timer.start();
    while (!job->done.load() && timer.elapsed() < kTranscodeHangTimeoutMs) {
        QTest::qWait(kTranscodeHangPollMs);
    }
    if (!job->done.load()) {
        worker.detach();
        return false;
    }
    worker.join();
    *result = job->result;
    return true;
}

double decodedEndMs(const DecodedAudio& audio)
{
    return (audio.firstFrame + static_cast<qint64>(audio.mono.size())) * kMsPerSecond / audio.sampleRate;
}

// Every failure is reported with qWarning; `pattern` pins its cause.
void expectFailureWarning(const char* pattern)
{
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QLatin1String(pattern), QRegularExpression::CaseInsensitiveOption));
}

} // namespace

class tst_VideoTranscoder : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void probeReportsFixture();
    void cancelStopsInsideAGapFill();
    void fixtureHasTimedPulses();
    void cropAndTrimKeepsAudio();
    void cropOnlyKeepsPrimedAudio();
    void trimStartBetweenAacPacketsKeepsAlignment();
    void earlyEndingAudio_data();
    void earlyEndingAudio();
    void probeReportsAudioRange();
    void rangeWithoutSourceAudioExportsVideoOnly_data();
    void rangeWithoutSourceAudioExportsVideoOnly();
    void trimWithoutAudio();
    void oddCropIsEvenAligned();
    void cancelRemovesOutput();
    void cancelWithAudioRemovesOutputAndKeepsSource_data();
    void cancelWithAudioRemovesOutputAndKeepsSource();
    void cancelAtCompletionRemovesOutput();
    void audioFaultFailsSafely_data();
    void audioFaultFailsSafely();
    void retryAfterAudioFailureSucceeds();
    void outputAliasingInputIsRefused();
    void invalidInputFails();
    void outputOptionsScaleAndLowerTheFrameRate();
    void lowerFrameRateKeepsTheCadence();
    void fasterPartKeepsTheFrameRate();
    void hevcOutput();
    void windowsExportsH264Only();
    void sizeEstimateAgainstActual();
    void segmentsStayInSync();
    void segmentsOfAVariableRateSource();
    void separateTracksAreRemixed();

private:
    std::unique_ptr<IVideoTranscoder> m_transcoder;
    QTemporaryDir m_dir;
    QString fixture(bool withAudio);
    QString earlyAudioFixture();
    VideoTranscoderFaultInjection* faultInjection();
    void verifyAudioPulses(const QString& outputPath, qint64 startMs, qint64 endMs,
                           const QString& sourcePath = QString());
};

void tst_VideoTranscoder::init()
{
    m_transcoder = createTestTranscoder();
    if (!m_transcoder) {
        QSKIP("No native transcoder on this platform");
    }
    QVERIFY(m_dir.isValid());
}

QString tst_VideoTranscoder::fixture(bool withAudio)
{
    const QString path = m_dir.filePath(withAudio ? QStringLiteral("av.mp4") : QStringLiteral("v.mp4"));
    if (QFileInfo::exists(path)) {
        return path;
    }
    const QString error = createFixture(path, withAudio);
    if (error.startsWith(QLatin1String("SKIP:"))) {
        return QString();
    }
    return error.isEmpty() ? path : QStringLiteral("ERROR:") + error;
}

QString tst_VideoTranscoder::earlyAudioFixture()
{
    const QString path = m_dir.filePath(QStringLiteral("av-early.mp4"));
    if (QFileInfo::exists(path)) {
        return path;
    }
    const QString error = createFixture(path, true, kEarlyAudioFrameCount);
    if (error.startsWith(QLatin1String("SKIP:"))) {
        return QString();
    }
    return error.isEmpty() ? path : QStringLiteral("ERROR:") + error;
}

VideoTranscoderFaultInjection* tst_VideoTranscoder::faultInjection()
{
    return dynamic_cast<VideoTranscoderFaultInjection*>(m_transcoder.get());
}

// Decodes the source (default: the full-audio fixture) and output audio and
// checks that the output holds the selected interval: audio from 0 to the
// interval end (or to the end of the source audio, if that comes first), and
// exactly the source bursts inside the interval, each at (source onset - startMs).
void tst_VideoTranscoder::verifyAudioPulses(const QString& outputPath, qint64 startMs, qint64 endMs,
                                            const QString& sourcePath)
{
    DecodedAudio source;
    QString error;
    QVERIFY2(decodeAudioTrack(sourcePath.isEmpty() ? fixture(true) : sourcePath, kAudioSampleRate, &source, &error),
             qPrintable(error));
    DecodedAudio output;
    QVERIFY2(decodeAudioTrack(outputPath, kAudioSampleRate, &output, &error), qPrintable(error));

    const double spanMs = static_cast<double>(endMs - startMs);
    const double coveredMs = std::min(spanMs, decodedEndMs(source) - startMs);
    const double outputStartMs = output.firstFrame * kMsPerSecond / output.sampleRate;
    const double outputEndMs = decodedEndMs(output);
    QVERIFY2(std::fabs(outputStartMs) <= kAudioEdgeToleranceMs, qPrintable(QString::number(outputStartMs)));
    QVERIFY2(outputEndMs >= coveredMs - kAudioEdgeToleranceMs,
             qPrintable(QStringLiteral("output audio ends at %1 ms, source audio covers %2 ms")
                            .arg(outputEndMs)
                            .arg(coveredMs)));

    std::vector<double> expected;
    for (double onset : pulseOnsetsMs(source)) {
        if (onset >= startMs && onset + kPulseDurationMs <= endMs) {
            expected.push_back(onset - startMs);
        }
    }
    const std::vector<double> actual = pulseOnsetsMs(output);
    const QString detail = QStringLiteral("expected [%1] actual [%2]").arg(describe(expected), describe(actual));
    QVERIFY2(!expected.empty(), qPrintable(detail));
    QVERIFY2(actual.size() == expected.size(), qPrintable(detail));
    for (size_t i = 0; i < expected.size(); ++i) {
        QVERIFY2(std::fabs(actual[i] - expected[i]) <= kPulseAlignmentToleranceMs, qPrintable(detail));
    }
}

void tst_VideoTranscoder::probeReportsFixture()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const VideoFileProbe probe = m_transcoder->probe(input);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kSourceSize);
    QVERIFY(qAbs(probe.durationMs - kFrameCount * kFrameIntervalMs) <= kDurationToleranceMs);
    QVERIFY(probe.hasAudio);
    // The fixture is encoded at kFrameRate fps as H.264; Phase 3's output
    // bitrate contract needs both facts from the probe.
    QVERIFY2(qAbs(probe.frameRate - kFrameRate) < kFrameRateTolerance,
             qPrintable(QStringLiteral("probed fps %1").arg(probe.frameRate)));
    QCOMPARE(probe.videoCodec, QStringLiteral("h264"));
}

void tst_VideoTranscoder::fixtureHasTimedPulses()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    DecodedAudio source;
    QString error;
    QVERIFY2(decodeAudioTrack(input, kAudioSampleRate, &source, &error), qPrintable(error));
    const std::vector<double> onsets = pulseOnsetsMs(source);
    QVERIFY2(onsets.size() == static_cast<size_t>(kPulseCount), qPrintable(describe(onsets)));
    for (int i = 0; i < kPulseCount; ++i) {
        const double nominal = kPulseFirstMs + i * kPulseSpacingMs;
        QVERIFY2(std::fabs(onsets[static_cast<size_t>(i)] - nominal) <= kFixturePulseToleranceMs,
                 qPrintable(describe(onsets)));
    }
}

void tst_VideoTranscoder::cropAndTrimKeepsAudio()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("out.mp4"));
    request.startMs = 500;
    request.endMs = 1500;
    request.cropRect = QRect(80, 0, 80, 60); // top-right = green
    int lastPercent = -1;
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        lastPercent = percent;
        return true;
    });
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    QCOMPARE(result.startMs, request.startMs); // 500 ms is on the frame grid
    QCOMPARE(lastPercent, 100);

    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, QSize(80, 60));
    QVERIFY2(qAbs(probe.durationMs - 1000) <= kDurationToleranceMs,
             qPrintable(QString::number(probe.durationMs)));
    QVERIFY(probe.hasAudio);

    if (auto reader = IVideoFrameReader::create()) {
        QVERIFY(reader->load(request.outputPath));
        const QImage frame = reader->frameAt(100);
        QVERIFY(!frame.isNull());
        QVERIFY2(isGreen(frame.pixelColor(frame.rect().center())),
                 qPrintable(frame.pixelColor(frame.rect().center()).name()));
    }
#ifdef Q_OS_WIN
    else {
        // No IVideoFrameReader on Windows: check the crop position (and row
        // orientation) on the decoded output frames instead.
        std::vector<DecodedVideoPixel> pixels;
        QString error;
        QVERIFY2(decodeVideoPixels(request.outputPath, QRect(QPoint(0, 0), probe.videoSize).center(), &pixels,
                                   &error),
                 qPrintable(error));
        QVERIFY(!pixels.empty());
        const QColor center(pixels.front().color);
        QVERIFY2(isGreen(center), qPrintable(center.name()));
    }
#endif

    verifyAudioPulses(request.outputPath, request.startMs, request.endMs);
}

// Start 0 copies the AAC priming packets, which carry the source's
// TrimDurationAtStart; a lost priming trim would shift every burst by 44 ms.
void tst_VideoTranscoder::cropOnlyKeepsPrimedAudio()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("croponly.mp4"));
    request.cropRect = QRect(0, 0, 80, 60);
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    const qint64 sourceDurationMs = m_transcoder->probe(input).durationMs;
    QCOMPARE(m_transcoder->probe(request.outputPath).videoSize, QSize(80, 60));
    verifyAudioPulses(request.outputPath, 0, sourceDurationMs);
}

void tst_VideoTranscoder::trimStartBetweenAacPacketsKeepsAlignment()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("unaligned.mp4"));
    request.startMs = kUnalignedStartMs;
    request.endMs = kUnalignedEndMs;
    // Keeps the bottom-right marker quadrant (source 80..160 x 60..120).
    request.cropRect = QRect(40, 30, 120, 90);
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    // The output starts on the frame shown at the requested start: exactly
    // there (AVFoundation) or, in a constant-frame-rate container, up to one
    // frame earlier (Media Foundation). Audio must share that origin.
    QVERIFY2(result.startMs <= request.startMs && request.startMs - result.startMs < kFrameIntervalMs,
             qPrintable(QString::number(result.startMs)));
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QCOMPARE(probe.videoSize, QSize(120, 90));
    QVERIFY2(qAbs(probe.durationMs - (kUnalignedEndMs - result.startMs)) <= kDurationToleranceMs,
             qPrintable(QString::number(probe.durationMs)));

    verifyAudioPulses(request.outputPath, result.startMs, request.endMs);
    if (QTest::currentTestFailed()) {
        return;
    }

    // A/V alignment: every burst starts with its marker frame.
    DecodedAudio output;
    QString error;
    QVERIFY2(decodeAudioTrack(request.outputPath, kAudioSampleRate, &output, &error), qPrintable(error));
    const std::vector<double> audioOnsets = pulseOnsetsMs(output);
    const QPoint markerPoint(kSourceSize.width() * 3 / 4 - request.cropRect.x(),
                             kSourceSize.height() * 3 / 4 - request.cropRect.y());
    const std::vector<double> videoOnsets = markerOnsetsMs(request.outputPath, markerPoint, probe.durationMs, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString detail = QStringLiteral("audio [%1] video [%2]").arg(describe(audioOnsets), describe(videoOnsets));
    QVERIFY2(audioOnsets.size() == videoOnsets.size(), qPrintable(detail));
    for (size_t i = 0; i < audioOnsets.size(); ++i) {
        QVERIFY2(std::fabs(audioOnsets[i] - videoOnsets[i]) <= kAvSyncToleranceMs, qPrintable(detail));
    }
}

// Export contract for source audio that ends before the video: an interval
// the source audio covers only in part succeeds, with the output audio
// covering exactly the audio-bearing part; the rest of the output is video
// only, as in the source. Validation compares the output audio with the
// source audio coverage inside the interval, not with the interval length,
// so this is not a silent downgrade. The transcode runs under a hang guard:
// on Windows this is the case that exercises the throttled Sink Writer with
// an audio stream that has ended while video is still being written.
void tst_VideoTranscoder::earlyEndingAudio_data()
{
    QTest::addColumn<qint64>("startMs");
    QTest::addColumn<qint64>("endMs");
    QTest::addColumn<QRect>("cropRect");
    QTest::addColumn<QString>("outputName");
    QTest::newRow("crop over the whole video")
        << qint64(0) << qint64(-1) << QRect(80, 0, 80, 60) << QStringLiteral("early-crop.mp4");
    QTest::newRow("trim across the audio end")
        << qint64(500) << qint64(1500) << QRect() << QStringLiteral("early-trim.mp4");
}

void tst_VideoTranscoder::earlyEndingAudio()
{
    QFETCH(qint64, startMs);
    QFETCH(qint64, endMs);
    QFETCH(QRect, cropRect);
    QFETCH(QString, outputName);
    const QString input = earlyAudioFixture();
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));

    // The fixture really ends its audio early.
    const VideoFileProbe sourceProbe = m_transcoder->probe(input);
    QVERIFY(sourceProbe.valid);
    QVERIFY(sourceProbe.hasAudio);
    DecodedAudio source;
    QString error;
    QVERIFY2(decodeAudioTrack(input, kAudioSampleRate, &source, &error), qPrintable(error));
    const double sourceAudioEndMs = decodedEndMs(source);
    QVERIFY2(std::fabs(sourceAudioEndMs - kEarlyAudioEndMs) <= kAudioEdgeToleranceMs,
             qPrintable(QString::number(sourceAudioEndMs)));
    QVERIFY2(sourceProbe.durationMs - sourceAudioEndMs >= kEarlyAudioMinGapMs - kAudioEdgeToleranceMs,
             qPrintable(QString::number(sourceProbe.durationMs)));

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(outputName);
    request.startMs = startMs;
    request.endMs = endMs;
    request.cropRect = cropRect;
    VideoTranscodeResult result;
    QVERIFY2(transcodeWithHangGuard(request, &result),
             qPrintable(QStringLiteral("transcode() did not return within %1 ms").arg(kTranscodeHangTimeoutMs)));
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    QVERIFY2(result.startMs <= startMs && startMs - result.startMs < kFrameIntervalMs,
             qPrintable(QString::number(result.startMs)));

    const qint64 expectedEndMs = endMs < 0 ? sourceProbe.durationMs : endMs;
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, cropRect.isEmpty() ? kSourceSize : cropRect.size());
    // The video keeps the whole interval, past the end of the audio.
    QVERIFY2(qAbs(probe.durationMs - (expectedEndMs - result.startMs)) <= kDurationToleranceMs,
             qPrintable(QString::number(probe.durationMs)));
    QVERIFY(probe.hasAudio);

    // Audio from 0 to the end of the source audio, with its bursts in place...
    verifyAudioPulses(request.outputPath, result.startMs, expectedEndMs, input);
    if (QTest::currentTestFailed()) {
        return;
    }
    // ...and nothing invented after it.
    DecodedAudio output;
    QVERIFY2(decodeAudioTrack(request.outputPath, kAudioSampleRate, &output, &error), qPrintable(error));
    QVERIFY2(decodedEndMs(output) <= sourceAudioEndMs - result.startMs + kAudioEdgeToleranceMs,
             qPrintable(QString::number(decodedEndMs(output))));
    QVERIFY(QFileInfo::exists(input));
}

// probe() reports where the first audio track lies on the presentation
// timeline, so a caller can tell a selection the source audio never reaches
// from one whose audio was lost.
void tst_VideoTranscoder::probeReportsAudioRange()
{
    const QString silent = fixture(false);
    QVERIFY2(!silent.isEmpty() && !silent.startsWith(QLatin1String("ERROR:")), qPrintable(silent));
    const VideoFileProbe silentProbe = m_transcoder->probe(silent);
    QVERIFY(silentProbe.valid);
    QVERIFY(!silentProbe.hasAudio);
    QCOMPARE(silentProbe.audioStartMs, qint64(-1));
    QCOMPARE(silentProbe.audioEndMs, qint64(-1));

    const QString early = earlyAudioFixture();
    if (early.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!early.startsWith(QLatin1String("ERROR:")), qPrintable(early));
    const VideoFileProbe probe = m_transcoder->probe(early);
    QVERIFY(probe.valid);
    QVERIFY(probe.hasAudio);
    // Starts with the video and ends where the encoder stopped getting audio,
    // well before the video does.
    QVERIFY2(qAbs(probe.audioStartMs) <= kAudioRangeToleranceMs, qPrintable(QString::number(probe.audioStartMs)));
    QVERIFY2(qAbs(probe.audioEndMs - kEarlyAudioEndMs) <= kAudioRangeToleranceMs,
             qPrintable(QString::number(probe.audioEndMs)));
    QVERIFY2(probe.durationMs - probe.audioEndMs >= kEarlyAudioMinGapMs - kAudioRangeToleranceMs,
             qPrintable(QStringLiteral("audio ends at %1 ms of %2 ms").arg(probe.audioEndMs).arg(probe.durationMs)));
}

// A selection that starts after the source audio ends has no audio to
// preserve: the export must succeed as video only, exactly like the source
// is over that range, instead of being refused as lost audio.
void tst_VideoTranscoder::rangeWithoutSourceAudioExportsVideoOnly_data()
{
    QTest::addColumn<qint64>("startMs");
    QTest::addColumn<bool>("mustBeVideoOnly");
    QTest::addColumn<QString>("outputName");
    QTest::newRow("after the audio end") << qint64(1000) << true << QStringLiteral("early-tail.mp4");
    // Inside the last packet: whether that edge packet is copied is up to the
    // platform, but the export must still succeed and stay self-consistent.
    QTest::newRow("inside the last audio packet")
        << qint64(kEarlyAudioEndMs - 5) << false << QStringLiteral("early-edge.mp4");
}

void tst_VideoTranscoder::rangeWithoutSourceAudioExportsVideoOnly()
{
    QFETCH(qint64, startMs);
    QFETCH(bool, mustBeVideoOnly);
    QFETCH(QString, outputName);
    const QString input = earlyAudioFixture();
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const VideoFileProbe sourceProbe = m_transcoder->probe(input);
    QVERIFY(sourceProbe.valid);
    QVERIFY(sourceProbe.hasAudio);
    const QByteArray sourceHash = fileHash(input);

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(outputName);
    request.startMs = startMs;
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));

    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kSourceSize);
    QVERIFY2(qAbs(probe.durationMs - (sourceProbe.durationMs - startMs)) <= kDurationToleranceMs,
             qPrintable(QString::number(probe.durationMs)));
    QCOMPARE(probe.hasAudio, result.audioCopied);
    if (mustBeVideoOnly) {
        QVERIFY(!result.audioCopied);
        QVERIFY(!probe.hasAudio);
    }
    QCOMPARE(fileHash(input), sourceHash);
}

void tst_VideoTranscoder::trimWithoutAudio()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("trim.mp4"));
    request.startMs = 1000;
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(!result.audioCopied);
    QCOMPARE(result.startMs, request.startMs); // 1000 ms is on the frame grid
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QCOMPARE(probe.videoSize, kSourceSize);
    QVERIFY(qAbs(probe.durationMs - 1000) <= kDurationToleranceMs);
    QVERIFY(!probe.hasAudio);
}

// The backend only sends even crops; anything else must not crash and is
// floored to the even size H.264 4:2:0 can encode.
void tst_VideoTranscoder::oddCropIsEvenAligned()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("odd.mp4"));
    request.cropRect = QRect(81, 1, 79, 59);
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(m_transcoder->probe(request.outputPath).videoSize, QSize(78, 58));
}

void tst_VideoTranscoder::cancelRemovesOutput()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("cancelled.mp4"));
    std::atomic<int> calls{0};
    std::atomic<int> maxPercent{-1};
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        ++calls;
        maxPercent = qMax(maxPercent.load(), percent);
        return false;
    });
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(!QFileInfo::exists(request.outputPath));
    // Cancelled on the first report, well before completion.
    QCOMPARE(calls.load(), 1);
    QVERIFY(maxPercent.load() < 100);
}

void tst_VideoTranscoder::cancelWithAudioRemovesOutputAndKeepsSource_data()
{
    QTest::addColumn<bool>("edited");
    QTest::newRow("plain trim") << false;
    QTest::newRow("edited audio and video") << true;
}

void tst_VideoTranscoder::cancelWithAudioRemovesOutputAndKeepsSource()
{
    QFETCH(bool, edited);
    if (edited && !m_transcoder->supportsFrameFilter()) QSKIP("Frame filtering unavailable");
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const QByteArray sourceHash = fileHash(input);

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("cancelled-av.mp4"));
    request.startMs = 200;
    if (edited) request.frameFilter = [](QImage&, qint64) {};
    std::atomic<int> maxPercent{-1};
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        maxPercent = qMax(maxPercent.load(), percent);
        if (percent < kCancelAtPercent) {
            return true;
        }
        return false;
    });
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    // Atomic exporters keep partial media in staging, outside outputPath.
    QVERIFY(maxPercent.load() >= kCancelAtPercent);
    QVERIFY(maxPercent.load() < 100);
    QVERIFY(!QFileInfo::exists(request.outputPath));
    QCOMPARE(fileHash(input), sourceHash);
}

// 100 is reported only for a finished, validated file. Declining it still
// cancels, so the finished output must be removed rather than kept.
void tst_VideoTranscoder::cancelAtCompletionRemovesOutput()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("declined.mp4"));
    request.startMs = 500;
    bool completionReported = false;
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        if (percent < 100) {
            return true;
        }
        completionReported = true;
        return false;
    });
    QVERIFY(!result.success);
    QVERIFY(completionReported);
    QVERIFY(!QFileInfo::exists(request.outputPath));
}

void tst_VideoTranscoder::audioFaultFailsSafely_data()
{
    QTest::addColumn<int>("fault");
    QTest::newRow("unsupported audio input") << static_cast<int>(VideoTranscodeFault::AudioInputUnsupported);
    QTest::newRow("audio retime failure") << static_cast<int>(VideoTranscodeFault::AudioRetimeFailure);
    QTest::newRow("audio append failure") << static_cast<int>(VideoTranscodeFault::AudioAppendFailure);
}

void tst_VideoTranscoder::audioFaultFailsSafely()
{
    QFETCH(int, fault);
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscoderFaultInjection* injection = faultInjection();
    QVERIFY2(injection, "Native transcoder must implement VideoTranscoderFaultInjection");
    injection->setFaultForTesting(static_cast<VideoTranscodeFault>(fault));
    const QByteArray sourceHash = fileHash(input);

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("fault.mp4"));
    request.startMs = 500;
    request.endMs = 1500;
    request.cropRect = QRect(80, 0, 80, 60);
    bool completed = false;
    expectFailureWarning("audio");
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        completed = completed || percent == 100;
        return true;
    });
    QVERIFY(!result.success);
    QVERIFY(!result.audioCopied);
    QVERIFY2(result.errorMessage.contains(QLatin1String("audio"), Qt::CaseInsensitive),
             qPrintable(result.errorMessage));
    QVERIFY(!completed);
    QVERIFY(!QFileInfo::exists(request.outputPath));
    QCOMPARE(fileHash(input), sourceHash);
    QVERIFY(m_transcoder->probe(input).hasAudio);
}

void tst_VideoTranscoder::retryAfterAudioFailureSucceeds()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscoderFaultInjection* injection = faultInjection();
    QVERIFY2(injection, "Native transcoder must implement VideoTranscoderFaultInjection");

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("retry.mp4"));
    request.startMs = 500;
    request.endMs = 1500;
    request.cropRect = QRect(80, 0, 80, 60);

    injection->setFaultForTesting(VideoTranscodeFault::AudioAppendFailure);
    expectFailureWarning("audio");
    const VideoTranscodeResult failed = m_transcoder->transcode(request, {});
    QVERIFY(!failed.success);
    QVERIFY(!QFileInfo::exists(request.outputPath));

    injection->setFaultForTesting(VideoTranscodeFault::None);
    const VideoTranscodeResult retried = m_transcoder->transcode(request, {});
    QVERIFY2(retried.success, qPrintable(retried.errorMessage));
    QVERIFY(retried.audioCopied);
    QCOMPARE(m_transcoder->probe(request.outputPath).videoSize, QSize(80, 60));
    verifyAudioPulses(request.outputPath, request.startMs, request.endMs);
}

void tst_VideoTranscoder::outputAliasingInputIsRefused()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const QByteArray sourceHash = fileHash(input);
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = input;
    request.startMs = 500;
    expectFailureWarning("path");
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    QCOMPARE(fileHash(input), sourceHash);

#ifdef Q_OS_UNIX
    // The same file reached through a different spelling of its path
    // (QFile::link makes a .lnk shortcut, not an alias, on Windows).
    const QString alias = m_dir.filePath(QStringLiteral("v-alias.mp4"));
    QVERIFY(QFile::link(input, alias));
    request.outputPath = alias;
    expectFailureWarning("path");
    const VideoTranscodeResult aliased = m_transcoder->transcode(request, {});
    QVERIFY(!aliased.success);
    QCOMPARE(fileHash(input), sourceHash);
#endif
}

void tst_VideoTranscoder::invalidInputFails()
{
    VideoTranscodeRequest request;
    request.inputPath = m_dir.filePath(QStringLiteral("missing.mp4"));
    request.outputPath = m_dir.filePath(QStringLiteral("never.mp4"));
    expectFailureWarning("input");
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(!QFileInfo::exists(request.outputPath));
    QVERIFY(!m_transcoder->probe(request.inputPath).valid);
}

void tst_VideoTranscoder::cancelStopsInsideAGapFill()
{
    // PR #175 review: drawing effects fills source gaps at the frame rate,
    // and that loop did not check for Cancel.
    auto transcoder = createTestTranscoder();
    if (!transcoder || !transcoder->supportsFrameFilter()) QSKIP("No frame-filter transcoder on this platform");
    QTemporaryDir dir;
    const QString input = dir.filePath(QStringLiteral("gap.mp4"));
    {
        std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
        QVERIFY(encoder && encoder->start(input, QSize(64, 48), 30));
        QImage frame(64, 48, QImage::Format_ARGB32);
        frame.fill(Qt::green);
        // A second at 30 fps, then nothing until 20 s (a still screen).
        for (int i = 0; i < 30; ++i) encoder->writeFrame(frame, i * 33);
        encoder->writeFrame(frame, 20000);
        QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
        encoder->finish();
        QVERIFY(!finished.isEmpty() || finished.wait(10000));
    }
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = dir.filePath(QStringLiteral("out.mp4"));
    int filtered = 0;
    request.frameFilter = [&filtered](QImage&, qint64) { ++filtered; };
    // Cancel once progress passes 20 %: only reachable inside the gap.
    const auto result = transcoder->transcode(request, [](int percent) { return percent < 20; });
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("cancelled")));
    // Stopped early in the gap (~20 %), not after filling all ~570 frames of it.
    QVERIFY2(filtered < 300, qPrintable(QString::number(filtered)));
    QVERIFY(!QFileInfo::exists(request.outputPath));
}

// #190: a smaller picture and a lower frame rate, with the audio kept.
void tst_VideoTranscoder::outputOptionsScaleAndLowerTheFrameRate()
{
    if (!m_transcoder->supportsOutputOptions()) QSKIP("This transcoder does not apply output options");
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("smaller.mp4"));
    request.cropRect = QRect(0, 0, 120, 100);
    request.outputSize = QSize(60, 50);
    request.frameRate = 5;
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QCOMPARE(probe.videoSize, QSize(60, 50));
    QVERIFY2(qAbs(probe.frameRate - 5.0) <= kFrameRateTolerance, qPrintable(QString::number(probe.frameRate)));
    QVERIFY(qAbs(probe.durationMs - kFrameCount * kFrameIntervalMs) <= kDurationToleranceMs);
    QVERIFY(probe.hasAudio);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecH264));
    // Never larger than the crop, and even.
    request.outputPath = m_dir.filePath(QStringLiteral("bounded.mp4"));
    request.outputSize = QSize(301, 99);
    request.frameRate = 0;
    const auto bounded = m_transcoder->transcode(request, {});
    QVERIFY2(bounded.success, qPrintable(bounded.errorMessage));
    QCOMPARE(m_transcoder->probe(request.outputPath).videoSize, QSize(120, 98));
}

// PR #197 review: a non-integer ratio keeps the requested cadence. 24 fps
// exported at 15 keeps 5 of every 8 frames, not every other one (12 fps).
void tst_VideoTranscoder::lowerFrameRateKeepsTheCadence()
{
    if (!m_transcoder->supportsOutputOptions() || !m_transcoder->supportsFrameFilter())
        QSKIP("This transcoder does not lower the frame rate itself");
    constexpr int kSourceFps = 24;
    constexpr int kSeconds = 2;
    const QString input = m_dir.filePath(QStringLiteral("24fps.mp4"));
    {
        std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
        QVERIFY(encoder);
        QVERIFY(encoder->start(input, kSourceSize, kSourceFps));
        for (int i = 0; i < kSourceFps * kSeconds; ++i) {
            const qint64 before = encoder->framesWritten();
            QElapsedTimer wait;
            wait.start();
            do {
                encoder->writeFrame(fixtureFrame(i % kFrameCount), qint64(i) * 1000 / kSourceFps);
                if (encoder->framesWritten() != before) break;
                QTest::qWait(5);
            } while (wait.elapsed() < 2000);
        }
        QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
        encoder->finish();
        QVERIFY(!finished.isEmpty() || finished.wait(10000));
    }
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("15fps.mp4"));
    request.frameRate = 15;
    int kept = 0;
    request.frameFilter = [&kept](QImage&, qint64) { ++kept; };
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    // 15 fps over 2 s is 30 frames; the old threshold kept about 24.
    QVERIFY2(qAbs(kept - 15 * kSeconds) <= 2, qPrintable(QString::number(kept)));
}

// PR #199 review: a part played faster keeps the output frame rate. A
// 30 fps recording at 1.5x is 1.33 s of output at 30 fps (40 frames), not
// every other source frame (about 30).
void tst_VideoTranscoder::fasterPartKeepsTheFrameRate()
{
    if (!m_transcoder->supportsSegments() || !m_transcoder->supportsFrameFilter())
        QSKIP("This transcoder does not join parts at other speeds");
    constexpr int kSourceFps = 30;
    constexpr int kSeconds = 2;
    const QString input = m_dir.filePath(QStringLiteral("30fps.mp4"));
    {
        std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
        QVERIFY(encoder);
        QVERIFY(encoder->start(input, kSourceSize, kSourceFps));
        for (int i = 0; i < kSourceFps * kSeconds; ++i) {
            const qint64 before = encoder->framesWritten();
            QElapsedTimer wait;
            wait.start();
            do {
                encoder->writeFrame(fixtureFrame(i % kFrameCount), qint64(i) * 1000 / kSourceFps);
                if (encoder->framesWritten() != before) break;
                QTest::qWait(5);
            } while (wait.elapsed() < 2000);
        }
        QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
        encoder->finish();
        QVERIFY(!finished.isEmpty() || finished.wait(10000));
    }
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("faster.mp4"));
    request.segments = {{0, kSeconds * 1000, 1.5}};
    int kept = 0;
    request.frameFilter = [&kept](QImage&, qint64) { ++kept; };
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY2(qAbs(kept - 40) <= 2, qPrintable(QString::number(kept)));
}

void tst_VideoTranscoder::windowsExportsH264Only()
{
#ifndef Q_OS_WIN
    QSKIP("Windows exports H.264 only");
#else
    QVERIFY(m_transcoder->supportsVideoCodec(QString::fromLatin1(kVideoCodecH264)));
    QVERIFY(!m_transcoder->supportsVideoCodec(QString::fromLatin1(kVideoCodecHevc)));
    VideoTranscodeRequest request;
    request.inputPath = QFINDTESTDATA("fixtures/hevc-main.mp4");
    QVERIFY(!request.inputPath.isEmpty());
    request.outputPath = m_dir.filePath(QStringLiteral("unsupported-hevc.mp4"));
    request.videoCodec = QString::fromLatin1(kVideoCodecHevc);
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY(!result.success);
    QCOMPARE(result.errorMessage, QStringLiteral("Unsupported video codec."));
    QVERIFY(!QFileInfo::exists(request.outputPath));
#endif
}

void tst_VideoTranscoder::hevcOutput()
{
    if (!m_transcoder->supportsVideoCodec(QString::fromLatin1(kVideoCodecHevc)))
        QSKIP("This system cannot export HEVC");
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("hevc.mp4"));
    request.videoCodec = QString::fromLatin1(kVideoCodecHevc);
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecHevc));
    QCOMPARE(probe.videoSize, kSourceSize);
    auto reader = IVideoFrameReader::create();
    QVERIFY(reader);
    QVERIFY2(reader->load(request.outputPath), qPrintable(reader->lastError()));
    QVERIFY(!reader->frameAt(500).isNull());
}

// The MP4 size estimate (#190) next to what the encoder really writes, for
// screen-like content: text blocks that scroll, a still screen (which the
// target-only estimate put about 10x too high), and a still screen that turns
// busy at the end, kept in parts (PR #213 review: an average over the whole
// recording put the busy part far too low). The sizes come from the recording
// as the platform's encoder wrote it, fragmented when it can (as recordings are).
void tst_VideoTranscoder::sizeEstimateAgainstActual()
{
    const QSize size(640, 360);
    constexpr int kFps = 30;
    constexpr int kSeconds = 6;
    constexpr int kBusyFromMs = 4000;
    const auto paintText = [](QImage& frame, int scroll) {
        QPainter painter(&frame);
        for (int line = 0; line < 30; ++line) {
            const int y = (line * 14 + scroll) % frame.height();
            for (int word = 0; word < 12; ++word) {
                const int w = 10 + ((line * 7 + word * 13) % 30);
                painter.fillRect(QRect(8 + word * 52, y, w, 8), QColor((line * 37) % 200, (word * 53) % 200, 90));
            }
        }
    };
    enum class Content { Scrolling, Still, StillThenBusy };
    struct Range { qint64 startMs; qint64 endMs; const char* name; };
    const struct {
        Content content;
        const char* name;
        std::vector<Range> ranges;
    } cases[] = {
        {Content::Scrolling, "scrolling", {{0, kSeconds * 1000, "all"}}},
        // PR #213 review: a short part between keyframes still costs a whole
        // first frame.
        {Content::Still, "still", {{0, kSeconds * 1000, "all"}, {1500, 1800, "300 ms mid-GOP"}}},
        {Content::StillThenBusy, "still-then-busy",
         {{0, kSeconds * 1000, "all"}, {0, kBusyFromMs, "still part"}, {kBusyFromMs, kSeconds * 1000, "busy part"}}},
    };
    for (const auto& testCase : cases) {
        const QString input = m_dir.filePath(QStringLiteral("%1.mp4").arg(QLatin1String(testCase.name)));
        {
            std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
            QVERIFY(encoder);
            encoder->setQuality(100);
            encoder->setFragmentedOutput(true); // where the platform records this way
            QVERIFY2(encoder->start(input, size, kFps), qPrintable(encoder->lastError()));
            for (int i = 0; i < kFps * kSeconds; ++i) {
                const int ms = i * 1000 / kFps;
                const bool moving = testCase.content == Content::Scrolling
                                    || (testCase.content == Content::StillThenBusy && ms >= kBusyFromMs);
                QImage frame(size, QImage::Format_RGB32);
                frame.fill(Qt::white);
                paintText(frame, moving ? i * 3 : 0);
                // Native encoders apply backpressure: retry until the frame lands.
                const qint64 before = encoder->framesWritten();
                QElapsedTimer wait;
                wait.start();
                do {
                    encoder->writeFrame(frame, ms);
                    if (encoder->framesWritten() != before) break;
                    QTest::qWait(5);
                } while (wait.elapsed() < 2000);
                QCOMPARE(encoder->framesWritten(), before + 1);
            }
            // macOS and Windows finish asynchronously.
            QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
            encoder->finish();
            QVERIFY(!finishedSpy.isEmpty() || finishedSpy.wait(10000));
            QVERIFY2(finishedSpy.first().at(0).toBool(), qPrintable(encoder->lastError()));
        }
        const VideoFileProbe probe = m_transcoder->probe(input);
        QVERIFY(probe.valid);
        // The sample table is read from the file as written: every frame, and
        // the video is most of a file without audio.
        const SnapTray::Mp4VideoSamples samples = SnapTray::Mp4VideoSamples::read(input);
        QCOMPARE(samples.sampleCount(), kFps * kSeconds);
        const qint64 fileBytes = QFileInfo(input).size();
        QVERIFY2(samples.totalBytes() <= fileBytes && samples.totalBytes() * 2 >= fileBytes,
                 qPrintable(QStringLiteral("%1 sample bytes in a %2-byte file").arg(samples.totalBytes()).arg(fileBytes)));
        QCOMPARE(samples.bytesBetween(0, kSeconds * 1000), samples.totalBytes());

        for (const Range& range : testCase.ranges) {
            for (const auto quality : {SnapTray::Mp4ExportOptions::Quality::Low, SnapTray::Mp4ExportOptions::Quality::High}) {
                SnapTray::Mp4ExportOptions options;
                options.quality = quality;
                VideoTranscodeRequest request;
                request.inputPath = input;
                request.outputPath = m_dir.filePath(QStringLiteral("estimate-%1-%2-%3.mp4")
                                                        .arg(QLatin1String(testCase.name)).arg(range.startMs)
                                                        .arg(int(quality)));
                request.startMs = range.startMs;
                request.endMs = range.endMs;
                request.videoBitrate = options.videoBitrate(size, kFps, 0);
                const VideoTranscodeResult result = m_transcoder->transcode(request, {});
                QVERIFY2(result.success, qPrintable(result.errorMessage));
                const qint64 actual = QFileInfo(request.outputPath).size();
                const qint64 durationMs = range.endMs - range.startMs;
                SnapTray::Mp4SizeEstimate::Source source;
                source.keptVideoBytes = samples.bytesToEncode(range.startMs, range.endMs);
                source.videoSize = probe.videoSize;
                source.hevc = probe.videoCodec == QLatin1String(kVideoCodecHevc);
                const qint64 targetOnly = SnapTray::Mp4SizeEstimate::bytes(request.videoBitrate, false, durationMs);
                const qint64 estimate = SnapTray::Mp4SizeEstimate::expectedBytes(
                    request.videoBitrate, false, source, size, false, durationMs);
                qInfo("MP4 estimate, %s, %s, quality %d: %lld estimated (target only %lld), %lld written (%.2fx)",
                      testCase.name, range.name, int(quality), estimate, targetOnly, actual,
                      double(actual) / double(estimate));
                // Encoders vary with the content: the estimate is a guide,
                // never wildly under. When it comes from the recording (below
                // the target), not wildly over either; at the target, simple
                // content may still stay well below it, as before.
                // A part under a second is mostly its first keyframe, and an
                // export's keyframes run larger than the recording's (about 2x
                // with VideoToolbox: 30 KB for a 14 KB source keyframe), so it
                // gets more room. Without that keyframe the 300 ms part was
                // estimated about 30x under.
                constexpr qint64 kShortPartMs = 1000;
                const qint64 headroom = durationMs < kShortPartMs ? 3 : 2;
                QVERIFY(actual > 0);
                QVERIFY2(actual <= estimate * headroom, "far above the estimate");
                if (estimate < targetOnly) QVERIFY2(actual * 4 >= estimate, "far below the estimate");
            }
        }
    }
}

// #185: parts joined at cuts, one at twice the speed. Bursts in the kept
// normal-speed parts land at their output times, each with its marker frame;
// the fast part is silent; nothing drifts across the seams.
void tst_VideoTranscoder::segmentsStayInSync()
{
    if (!m_transcoder->supportsSegments()) QSKIP("This transcoder does not join parts");
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("segments.mp4"));
    // Kept: 0-700 (1x), 1100-1500 (2x), 1500-2000 (1x) = 700 + 200 + 500 ms.
    request.segments = {{0, 700, 1.0}, {1100, 1500, 2.0}, {1500, 2000, 1.0}};
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QVERIFY(probe.valid);
    QVERIFY2(qAbs(probe.durationMs - 1400) <= kDurationToleranceMs, qPrintable(QString::number(probe.durationMs)));
    QVERIFY(probe.hasAudio);

    DecodedAudio output;
    QString error;
    QVERIFY2(decodeAudioTrack(request.outputPath, kAudioSampleRate, &output, &error), qPrintable(error));
    const std::vector<double> audioOnsets = pulseOnsetsMs(output);
    // Source bursts 200, 400, 600 | (1200, 1400 in the fast part: silent) | 1600, 1800.
    const std::vector<double> expected{200, 400, 600, 700 + 200 + 100, 700 + 200 + 300};
    QString detail = QStringLiteral("expected [%1] actual [%2]").arg(describe(expected), describe(audioOnsets));
    QVERIFY2(audioOnsets.size() == expected.size(), qPrintable(detail));
    for (size_t i = 0; i < expected.size(); ++i) {
        QVERIFY2(std::fabs(audioOnsets[i] - expected[i]) <= kPulseAlignmentToleranceMs, qPrintable(detail));
    }
    // Every burst still starts with its marker frame.
    const QPoint markerPoint(kSourceSize.width() * 3 / 4, kSourceSize.height() * 3 / 4);
    const std::vector<double> videoOnsets = markerOnsetsMs(request.outputPath, markerPoint, probe.durationMs, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    detail = QStringLiteral("audio [%1] video [%2]").arg(describe(audioOnsets), describe(videoOnsets));
    for (const double onset : audioOnsets) {
        const bool matched = std::any_of(videoOnsets.begin(), videoOnsets.end(),
                                         [onset](double v) { return std::fabs(v - onset) <= kAvSyncToleranceMs; });
        QVERIFY2(matched, qPrintable(detail));
    }
}

// #185 on a variable-frame-rate source with a long still stretch: the map is
// in time, not frames, so each output time shows the frame the source shows.
void tst_VideoTranscoder::segmentsOfAVariableRateSource()
{
    if (!m_transcoder->supportsSegments()) QSKIP("This transcoder does not join parts");
    // A checked-in VFR file keeps the fixture independent of native capture
    // encoders, some of which normalize the supplied timestamps.
    const QString input = QFINDTESTDATA("fixtures/edit-vfr.mp4");
    QVERIFY(!input.isEmpty());
#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
    if (auto* native = dynamic_cast<NativeEditTestTranscoder*>(m_transcoder.get())) {
        // Media Foundation reports the average rate, not the maximum rate.
        native->setSourceFrameRateForTesting(input, 80.0 / 17.0);
    }
#endif
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("vfr-segments.mp4"));
    // 100-700 (still red) | 1050-1450 at 2x (green, then blue at 1400 -> out 775).
    request.segments = {{100, 700, 1.0}, {1050, 1450, 2.0}};
    const QPoint centre = QRect(QPoint(0, 0), kSourceSize).center();
    // Check the fixture independently: some native encoders can rewrite VFR
    // timing. Include decoded transitions in failures to distinguish that
    // from the editing path or output reader.
    auto describeFrames = [](const QString& path) {
        auto frames = IVideoFrameReader::create();
        if (!frames || !frames->load(path)) return QStringLiteral("unreadable");
        QStringList details;
        qint64 at = 0;
        for (int i = 0; i < 40; ++i) {
            const QImage frame = frames->frameAtMicroseconds(at);
            if (frame.isNull()) break;
            details << QStringLiteral("%1:%2").arg(at).arg(frame.pixelColor(frame.rect().center()).name());
            const qint64 next = frames->nextFrameTimeMicroseconds();
            if (next <= at) break;
            at = next;
        }
        return details.join(QLatin1Char(' '));
    };
    auto sourceReader = IVideoFrameReader::create();
    QVERIFY(sourceReader && sourceReader->load(input));
    const QColor sourceStill = sourceReader->frameAt(600).pixelColor(centre);
    QVERIFY2(sourceStill.red() > 170 && sourceStill.green() < 90,
             qPrintable(QStringLiteral("Source timing: %1").arg(describeFrames(input))));
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    // Encoders may retain one nominal frame's tail. Native readers report
    // this VFR source as 80/17 fps, so 150 ms is less than one such frame.
    const double sourceFps = m_transcoder->probe(input).frameRate;
    QVERIFY(sourceFps > 0);
    const qint64 tailTolerance = qMax(kDurationToleranceMs, qint64(std::ceil(1000.0 / sourceFps)));
    QVERIFY2(qAbs(probe.durationMs - 800) <= tailTolerance, qPrintable(QString::number(probe.durationMs)));
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader to inspect the output here");
    QVERIFY2(reader->load(request.outputPath), qPrintable(reader->lastError()));
    const QColor still = reader->frameAt(500).pixelColor(centre);
    const QColor fast = reader->frameAt(650).pixelColor(centre);
    const QColor last = reader->frameAt(790).pixelColor(centre);
    QVERIFY2(still.red() > 170 && still.green() < 90,
             qPrintable(QStringLiteral("Source: %1; output: %2")
                 .arg(describeFrames(input), describeFrames(request.outputPath))));
    QVERIFY2(fast.green() > 120 && fast.red() < 90, qPrintable(fast.name()));
    QVERIFY2(last.blue() > 170 && last.green() < 90,
             qPrintable(QStringLiteral("Source: %1; output: %2")
                 .arg(describeFrames(input), describeFrames(request.outputPath))));
}

// #189: the separate tracks next to a recording are mixed again at export.
// The microphone track has the fixture's timed bursts, the system track a
// steady low tone; muting the system leaves only the bursts, at their times.
void tst_VideoTranscoder::separateTracksAreRemixed()
{
    if (!m_transcoder->supportsAudioMix()) QSKIP("This transcoder does not mix separate tracks");
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const QString video = m_dir.filePath(QStringLiteral("tracks.mp4"));
    QVERIFY(QFile::copy(input, video));
    const int frames = kFrameCount * kFrameIntervalMs * 48;
    QByteArray mic(frames * 4, '\0'), system(frames * 4, '\0');
    auto* m = reinterpret_cast<qint16*>(mic.data());
    auto* s = reinterpret_cast<qint16*>(system.data());
    for (int f = 0; f < frames; ++f) {
        const double t = f / 48000.0;
        const int ms = f / 48;
        const bool burst = ms >= kPulseFirstMs && (ms - kPulseFirstMs) % kPulseSpacingMs < kPulseDurationMs
            && (ms - kPulseFirstMs) / kPulseSpacingMs < kPulseCount;
        const auto b = qint16(burst ? std::lround(16000 * std::sin(2 * M_PI * kPulseFrequencyHz * t)) : 0);
        const auto low = qint16(std::lround(12000 * std::sin(2 * M_PI * 150.0 * t)));
        m[2 * f] = m[2 * f + 1] = b;
        s[2 * f] = s[2 * f + 1] = low;
    }
    {
        SnapTray::AudioTrackWriter micTrack(SnapTray::AudioTracks::pathFor(video, SnapTray::AudioTracks::Track::Microphone));
        SnapTray::AudioTrackWriter systemTrack(SnapTray::AudioTracks::pathFor(video, SnapTray::AudioTracks::Track::SystemAudio));
        QVERIFY(micTrack.open() && systemTrack.open());
        micTrack.write(mic, 0);
        systemTrack.write(system, 0);
    }
    VideoTranscodeRequest request;
    request.inputPath = video;
    request.outputPath = m_dir.filePath(QStringLiteral("remixed.mp4"));
    request.startMs = 300; // a trim moves the tracks with the video
    request.audioTracksVideoPath = video;
    request.audioMix.systemAudioMuted = true;
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    DecodedAudio output;
    QString error;
    QVERIFY2(decodeAudioTrack(request.outputPath, kAudioSampleRate, &output, &error), qPrintable(error));
    const std::vector<double> onsets = pulseOnsetsMs(output);
    std::vector<double> expected;
    for (int i = 0; i < kPulseCount; ++i) {
        const double at = kPulseFirstMs + i * kPulseSpacingMs - 300;
        if (at >= 0 && at + kPulseDurationMs <= kFrameCount * kFrameIntervalMs - 300) expected.push_back(at);
    }
    const QString detail = QStringLiteral("expected [%1] actual [%2]").arg(describe(expected), describe(onsets));
    QVERIFY2(onsets.size() == expected.size(), qPrintable(detail));
    for (size_t i = 0; i < expected.size(); ++i) {
        QVERIFY2(std::fabs(onsets[i] - expected[i]) <= kPulseAlignmentToleranceMs, qPrintable(detail));
    }
    // Between bursts: silence, the system tone is gone.
    double between = 0.0;
    // 160-260 ms of the output: after the burst at 100 ms, before the one at 300 ms.
    const qint64 from = qMax<qint64>(0, 160 * 48 - output.firstFrame);
    for (qint64 i = from; i < from + 4800 && i < qint64(output.mono.size()); ++i)
        between = std::max(between, double(std::fabs(output.mono[size_t(i)])));
    QVERIFY2(between < 0.02, qPrintable(QString::number(between)));
}

QTEST_MAIN(tst_VideoTranscoder)
#include "tst_VideoTranscoder.moc"
