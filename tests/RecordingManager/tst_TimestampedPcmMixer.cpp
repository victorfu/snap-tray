#include <QtTest/QtTest>

#include "capture/TimestampedPcmMixer.h"

#include <QtEndian>

using SnapTray::Audio::ByteOrder;
using SnapTray::Audio::InputChunk;
using SnapTray::Audio::OutputChunk;
using SnapTray::Audio::Pcm16Format;
using SnapTray::Audio::Source;
using SnapTray::Audio::TimestampedPcmMixer;

namespace {

constexpr qint64 kNsPerSecond = 1000000000LL;

QByteArray pcm16(std::initializer_list<qint16> samples)
{
    QByteArray data;
    data.reserve(static_cast<qsizetype>(samples.size() * 2));
    for (qint16 sample : samples) {
        char bytes[2];
        qToLittleEndian<quint16>(
            static_cast<quint16>(sample),
            reinterpret_cast<uchar *>(bytes));
        data.append(bytes, 2);
    }
    return data;
}

QByteArray monoRamp(int frames)
{
    QByteArray data;
    data.reserve(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const qint16 sample = static_cast<qint16>((i * 97) % 60000 - 30000);
        char bytes[2];
        qToLittleEndian<quint16>(
            static_cast<quint16>(sample),
            reinterpret_cast<uchar *>(bytes));
        data.append(bytes, 2);
    }
    return data;
}

QList<qint16> samples(const QByteArray& pcm)
{
    QList<qint16> result;
    const auto *bytes = reinterpret_cast<const uchar *>(pcm.constData());
    for (qsizetype offset = 0; offset + 1 < pcm.size(); offset += 2) {
        result.append(static_cast<qint16>(qFromLittleEndian<quint16>(bytes + offset)));
    }
    return result;
}

QByteArray join(const QVector<OutputChunk>& chunks)
{
    QByteArray result;
    for (const auto& chunk : chunks) {
        result.append(chunk.pcm);
    }
    return result;
}

qint64 nsForFrames(qint64 frames, int sampleRate)
{
    return frames * kNsPerSecond / sampleRate;
}

TimestampedPcmMixer::Config config(bool mic, bool system)
{
    TimestampedPcmMixer::Config value;
    value.microphoneEnabled = mic;
    value.systemAudioEnabled = system;
    value.outputChunkFrames = 4096;
    return value;
}

Pcm16Format format(int sampleRate, int channels)
{
    return {sampleRate, channels, ByteOrder::LittleEndian, true, true};
}

} // namespace

class tst_TimestampedPcmMixer : public QObject
{
    Q_OBJECT

private slots:
    void outputFormatIsCanonical();
    void rejectsMalformedAndUnsupportedInput();
    void singleSourceCanonicalizesMono();
    void multichannelInputUsesLayoutAgnosticAverage();
    void resamplingIsPartitionInvariant();
    void mixesSamplesWithSaturation();
    void alignsByTimestampAndFillsGaps();
    void trimsSameSourceOverlap();
    void pendingBufferIsCapped();
    void immediatelyDrainableInputIsNotCapped();
    void boundedSkewReleasesLeadingSource();
    void futureSourceEnableHonorsEffectiveTime();
    void futureOnlySourceDrainsAtActivation();
    void activationBoundaryThatTrimsWholePacketIsTooLate();
    void tinyOverlapDropsArePartitionInvariant();
    void fullyLateCanonicalSegmentReportsTooLate();
    void disablingSourceReleasesPeer();
    void pauseResumeClearsCrossPauseState();
    void flushClosesUntilReset();
};

void tst_TimestampedPcmMixer::outputFormatIsCanonical()
{
    const auto output = TimestampedPcmMixer::outputFormat();
    QCOMPARE(output.sampleRate, 48000);
    QCOMPARE(output.channels, 2);
    QCOMPARE(output.bitsPerSample, 16);
}

void tst_TimestampedPcmMixer::rejectsMalformedAndUnsupportedInput()
{
    TimestampedPcmMixer mixer(config(true, false));

    InputChunk malformed{pcm16({1}), 0, format(0, 1)};
    QCOMPARE(mixer.push(Source::Microphone, malformed).code,
             TimestampedPcmMixer::ResultCode::MalformedInput);

    InputChunk partial{QByteArray(3, '\0'), 0, format(48000, 1)};
    QCOMPARE(mixer.push(Source::Microphone, partial).code,
             TimestampedPcmMixer::ResultCode::MalformedInput);

    InputChunk bigEndian{pcm16({1}), 0, format(48000, 1)};
    bigEndian.format.byteOrder = ByteOrder::BigEndian;
    QCOMPARE(mixer.push(Source::Microphone, bigEndian).code,
             TimestampedPcmMixer::ResultCode::UnsupportedFormat);

    InputChunk planar{pcm16({1}), 0, format(48000, 1)};
    planar.format.interleaved = false;
    QCOMPARE(mixer.push(Source::Microphone, planar).code,
             TimestampedPcmMixer::ResultCode::UnsupportedFormat);

    InputChunk disabled{pcm16({1}), 0, format(48000, 1)};
    QCOMPARE(mixer.push(Source::SystemAudio, disabled).code,
             TimestampedPcmMixer::ResultCode::SourceDisabled);
}

void tst_TimestampedPcmMixer::singleSourceCanonicalizesMono()
{
    TimestampedPcmMixer mixer(config(true, false));
    const auto result = mixer.push(
        Source::Microphone,
        {pcm16({100, -200, 300}), 0, format(48000, 1)});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(result.output.size(), 1);
    QCOMPARE(samples(result.output.first().pcm),
             QList<qint16>({100, 100, -200, -200, 300, 300}));
}

void tst_TimestampedPcmMixer::multichannelInputUsesLayoutAgnosticAverage()
{
    TimestampedPcmMixer mixer(config(true, false));
    const auto result = mixer.push(
        Source::Microphone,
        {pcm16({300, 0, -300, 300, 600, 900}), 0, format(48000, 3)});

    QCOMPARE(samples(join(result.output)),
             QList<qint16>({0, 0, 600, 600}));
}

void tst_TimestampedPcmMixer::resamplingIsPartitionInvariant()
{
    const QByteArray input = monoRamp(4410);
    const Pcm16Format inputFormat = format(44100, 1);

    TimestampedPcmMixer whole(config(true, false));
    QVector<OutputChunk> wholeOutput = whole.push(
        Source::Microphone,
        {input, 0, inputFormat}).output;
    wholeOutput += whole.flush(100000000).output;

    TimestampedPcmMixer partitioned(config(true, false));
    QVector<OutputChunk> partitionedOutput;
    const int framePartitions[] = {731, 997, 2682};
    int consumedFrames = 0;
    for (int frameCount : framePartitions) {
        const QByteArray packet = input.mid(consumedFrames * 2, frameCount * 2);
        partitionedOutput += partitioned.push(
            Source::Microphone,
            {packet, nsForFrames(consumedFrames, 44100), inputFormat}).output;
        consumedFrames += frameCount;
    }
    partitionedOutput += partitioned.flush(100000000).output;

    const QByteArray wholePcm = join(wholeOutput);
    const QByteArray partitionedPcm = join(partitionedOutput);
    QCOMPARE(wholePcm, partitionedPcm);
    QCOMPARE(wholePcm.size() / 4, 4800);
}

void tst_TimestampedPcmMixer::mixesSamplesWithSaturation()
{
    TimestampedPcmMixer mixer(config(true, true));
    const Pcm16Format stereo = format(48000, 2);

    QVERIFY(mixer.push(Source::Microphone,
                       {pcm16({30000, -30000, 32767, -32768}), 0, stereo})
                .output.isEmpty());
    const auto result = mixer.push(
        Source::SystemAudio,
        {pcm16({10000, -10000, 1, -1}), 0, stereo});

    QCOMPARE(samples(join(result.output)),
             QList<qint16>({32767, -32768, 32767, -32768}));
}

void tst_TimestampedPcmMixer::alignsByTimestampAndFillsGaps()
{
    TimestampedPcmMixer mixer(config(true, true));
    const Pcm16Format stereo = format(48000, 2);

    QByteArray mic;
    for (int i = 0; i < 8; ++i) mic += pcm16({100, 100});
    QByteArray system;
    for (int i = 0; i < 4; ++i) system += pcm16({10, 10});

    QVector<OutputChunk> output = mixer.push(
        Source::Microphone, {mic, 0, stereo}).output;
    output += mixer.push(
        Source::SystemAudio, {system, nsForFrames(2, 48000), stereo}).output;
    output += mixer.setSourceEnabled(
        Source::SystemAudio, false, nsForFrames(8, 48000)).output;

    QList<qint16> expected;
    for (int frame = 0; frame < 8; ++frame) {
        const qint16 value = frame >= 2 && frame < 6 ? 110 : 100;
        expected << value << value;
    }
    QCOMPARE(samples(join(output)), expected);
}

void tst_TimestampedPcmMixer::trimsSameSourceOverlap()
{
    TimestampedPcmMixer mixer(config(true, false));
    const Pcm16Format mono = format(48000, 1);

    QVector<OutputChunk> output = mixer.push(
        Source::Microphone,
        {pcm16({1, 2, 3, 4}), 0, mono}).output;
    const auto overlap = mixer.push(
        Source::Microphone,
        {pcm16({30, 40, 50, 60}), nsForFrames(2, 48000), mono});
    output += overlap.output;

    QCOMPARE(overlap.code, TimestampedPcmMixer::ResultCode::AcceptedWithDrops);
    QCOMPARE(samples(join(output)),
             QList<qint16>({1, 1, 2, 2, 3, 3, 4, 4, 50, 50, 60, 60}));
}

void tst_TimestampedPcmMixer::pendingBufferIsCapped()
{
    auto cappedConfig = config(true, true);
    cappedConfig.maxPendingFramesPerSource = 4;
    cappedConfig.maxPendingBytesPerSource = 16;
    cappedConfig.maxSkewFrames = 1000;
    TimestampedPcmMixer mixer(cappedConfig);

    QByteArray mic;
    for (int i = 0; i < 10; ++i) mic += pcm16({100, 100});
    const auto result = mixer.push(
        Source::Microphone,
        {mic, 0, format(48000, 2)});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::AcceptedWithDrops);
    QVERIFY(result.droppedFrames >= 6);
    QVERIFY(mixer.pendingFrames(Source::Microphone) <= 4);
    QVERIFY(mixer.pendingBytes(Source::Microphone) <= 16);
    QVERIFY(mixer.stats().overflowFrames >= 6);
}

void tst_TimestampedPcmMixer::immediatelyDrainableInputIsNotCapped()
{
    auto smallCap = config(true, false);
    smallCap.maxPendingFramesPerSource = 4;
    smallCap.maxPendingBytesPerSource = 16;

    QByteArray input;
    for (int i = 0; i < 10; ++i) input += pcm16({qint16(i), qint16(i)});

    TimestampedPcmMixer whole(smallCap);
    const auto wholeResult = whole.push(
        Source::Microphone,
        {input, 0, format(48000, 2)});

    TimestampedPcmMixer partitioned(smallCap);
    QVector<OutputChunk> partitionedOutput;
    partitionedOutput += partitioned.push(
        Source::Microphone,
        {input.left(16), 0, format(48000, 2)}).output;
    partitionedOutput += partitioned.push(
        Source::Microphone,
        {input.mid(16, 16), nsForFrames(4, 48000), format(48000, 2)}).output;
    partitionedOutput += partitioned.push(
        Source::Microphone,
        {input.mid(32), nsForFrames(8, 48000), format(48000, 2)}).output;

    QCOMPARE(join(wholeResult.output), join(partitionedOutput));
    QCOMPARE(wholeResult.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(whole.stats().overflowFrames, qint64(0));
}

void tst_TimestampedPcmMixer::boundedSkewReleasesLeadingSource()
{
    auto skewConfig = config(true, true);
    skewConfig.maxSkewFrames = 2;
    TimestampedPcmMixer mixer(skewConfig);

    QByteArray mic;
    for (int i = 0; i < 6; ++i) mic += pcm16({100, 100});
    const auto result = mixer.push(
        Source::Microphone,
        {mic, 0, format(48000, 2)});

    QCOMPARE(join(result.output).size() / 4, 4);
    QCOMPARE(samples(join(result.output)),
             QList<qint16>({100, 100, 100, 100, 100, 100, 100, 100}));
    QCOMPARE(mixer.pendingFrames(Source::Microphone), qint64(2));
    QCOMPARE(mixer.stats().synthesizedGapFrames, qint64(4));
}

void tst_TimestampedPcmMixer::futureSourceEnableHonorsEffectiveTime()
{
    auto futureConfig = config(true, false);
    futureConfig.maxSkewFrames = 2;
    TimestampedPcmMixer mixer(futureConfig);

    mixer.setSourceEnabled(Source::SystemAudio, true, nsForFrames(10, 48000));
    QByteArray mic;
    for (int i = 0; i < 6; ++i) mic += pcm16({100, 100});
    const auto micResult = mixer.push(
        Source::Microphone,
        {mic, 0, format(48000, 2)});

    QCOMPARE(join(micResult.output).size() / 4, 6);
    QCOMPARE(mixer.stats().synthesizedGapFrames, qint64(0));

    const auto staleSystem = mixer.push(
        Source::SystemAudio,
        {pcm16({10, 10}), 0, format(48000, 2)});
    QCOMPARE(staleSystem.code, TimestampedPcmMixer::ResultCode::TooLate);
}

void tst_TimestampedPcmMixer::futureOnlySourceDrainsAtActivation()
{
    auto futureConfig = config(false, false);
    futureConfig.maxPendingFramesPerSource = 4;
    futureConfig.maxPendingBytesPerSource = 16;
    TimestampedPcmMixer mixer(futureConfig);

    mixer.setSourceEnabled(Source::Microphone, true, nsForFrames(10, 48000));
    QByteArray mic;
    for (int i = 0; i < 10; ++i) mic += pcm16({qint16(i), qint16(i)});
    const auto result = mixer.push(
        Source::Microphone,
        {mic, nsForFrames(10, 48000), format(48000, 2)});

    QCOMPARE(join(result.output).size() / 4, 10);
    QCOMPARE(result.output.first().startFrame, qint64(10));
    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(mixer.stats().overflowFrames, qint64(0));
}

void tst_TimestampedPcmMixer::activationBoundaryThatTrimsWholePacketIsTooLate()
{
    TimestampedPcmMixer mixer(config(false, false));
    mixer.setSourceEnabled(Source::Microphone, true, 20000);

    const auto result = mixer.push(
        Source::Microphone,
        {pcm16({100}), 0, format(44100, 1)});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::TooLate);
    QVERIFY(result.output.isEmpty());
    QCOMPARE(mixer.pendingFrames(Source::Microphone), qint64(0));
}

void tst_TimestampedPcmMixer::tinyOverlapDropsArePartitionInvariant()
{
    TimestampedPcmMixer mixer(config(true, false));
    const Pcm16Format highRate = format(192000, 1);
    mixer.push(Source::Microphone, {pcm16({100}), 0, highRate});

    for (int i = 0; i < 4; ++i) {
        const auto duplicate = mixer.push(
            Source::Microphone,
            {pcm16({100}), 0, highRate});
        QCOMPARE(duplicate.code, TimestampedPcmMixer::ResultCode::TooLate);
    }
    QCOMPARE(mixer.stats().overlapFrames, qint64(1));
}

void tst_TimestampedPcmMixer::fullyLateCanonicalSegmentReportsTooLate()
{
    TimestampedPcmMixer mixer(config(true, false));
    mixer.reset(10 * 1000000LL);

    QByteArray late;
    for (int i = 0; i < 48; ++i) late += pcm16({100, 100});
    const auto result = mixer.push(
        Source::Microphone,
        {late, 0, format(48000, 2)});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::TooLate);
    QVERIFY(result.output.isEmpty());
}

void tst_TimestampedPcmMixer::disablingSourceReleasesPeer()
{
    TimestampedPcmMixer mixer(config(true, true));
    const auto pending = mixer.push(
        Source::Microphone,
        {pcm16({1, 2, 3, 4}), 0, format(48000, 1)});
    QVERIFY(pending.output.isEmpty());

    const auto disabled = mixer.setSourceEnabled(
        Source::SystemAudio, false, nsForFrames(4, 48000));
    QCOMPARE(samples(join(disabled.output)),
             QList<qint16>({1, 1, 2, 2, 3, 3, 4, 4}));
    QCOMPARE(mixer.push(Source::SystemAudio,
                        {pcm16({1}), 0, format(48000, 1)}).code,
             TimestampedPcmMixer::ResultCode::SourceDisabled);
}

void tst_TimestampedPcmMixer::pauseResumeClearsCrossPauseState()
{
    TimestampedPcmMixer mixer(config(true, false));
    QVector<OutputChunk> beforePause = mixer.push(
        Source::Microphone,
        {pcm16({100, 200}), 0, format(44100, 1)}).output;
    beforePause += mixer.pause(nsForFrames(2, 44100)).output;

    QCOMPARE(mixer.push(Source::Microphone,
                        {pcm16({300}), 0, format(44100, 1)}).code,
             TimestampedPcmMixer::ResultCode::Paused);
    mixer.resume(nsForFrames(2, 44100));
    const auto afterResume = mixer.push(
        Source::Microphone,
        {pcm16({1000, 1000}), nsForFrames(2, 44100), format(44100, 1)});

    QVERIFY(!beforePause.isEmpty());
    const QList<qint16> resumedSamples = samples(join(afterResume.output));
    QVERIFY(!resumedSamples.isEmpty());
    QCOMPARE(resumedSamples.first(), qint16(1000));
}

void tst_TimestampedPcmMixer::flushClosesUntilReset()
{
    TimestampedPcmMixer mixer(config(true, false));
    mixer.push(Source::Microphone, {pcm16({10}), 0, format(24000, 1)});
    const auto flushed = mixer.flush(nsForFrames(2, 48000));
    QVERIFY(!flushed.output.isEmpty());
    QCOMPARE(mixer.flush(nsForFrames(2, 48000)).code,
             TimestampedPcmMixer::ResultCode::Closed);
    QCOMPARE(mixer.push(Source::Microphone,
                        {pcm16({10}), 0, format(24000, 1)}).code,
             TimestampedPcmMixer::ResultCode::Closed);

    mixer.reset();
    QCOMPARE(mixer.push(Source::Microphone,
                        {pcm16({10}), 0, format(24000, 1)}).code,
             TimestampedPcmMixer::ResultCode::Accepted);
}

QTEST_MAIN(tst_TimestampedPcmMixer)
#include "tst_TimestampedPcmMixer.moc"
