#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "VideoTranscoderTestAudio.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"
#include "video/VideoTranscoderFaultInjection.h"

#include <QCryptographicHash>
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

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 20;                  // 2 s
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerSample = 2;
constexpr int kAudioFramesPerVideoFrame = kAudioSampleRate / kFrameRate;
constexpr qint64 kDurationToleranceMs = 150;
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
        std::unique_ptr<IVideoTranscoder> transcoder = IVideoTranscoder::create();
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
    void cancelWithAudioRemovesOutputAndKeepsSource();
    void cancelAtCompletionRemovesOutput();
    void audioFaultFailsSafely_data();
    void audioFaultFailsSafely();
    void retryAfterAudioFailureSucceeds();
    void outputAliasingInputIsRefused();
    void invalidInputFails();

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
    m_transcoder = IVideoTranscoder::create();
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

void tst_VideoTranscoder::cancelWithAudioRemovesOutputAndKeepsSource()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const QByteArray sourceHash = fileHash(input);

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("cancelled-av.mp4"));
    request.startMs = 200;
    std::atomic<int> maxPercent{-1};
    std::atomic<bool> partialOutputExisted{false};
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        maxPercent = qMax(maxPercent.load(), percent);
        if (percent < kCancelAtPercent) {
            return true;
        }
        partialOutputExisted = QFileInfo::exists(request.outputPath);
        return false;
    });
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(partialOutputExisted.load());
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
    bool outputExistedAtCompletion = false;
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        if (percent < 100) {
            return true;
        }
        outputExistedAtCompletion = QFileInfo::exists(request.outputPath);
        return false;
    });
    QVERIFY(!result.success);
    QVERIFY(outputExistedAtCompletion);
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

QTEST_MAIN(tst_VideoTranscoder)
#include "tst_VideoTranscoder.moc"
