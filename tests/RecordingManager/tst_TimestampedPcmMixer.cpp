#include <QtTest/QtTest>

#include "capture/TimestampedPcmMixer.h"
#include "encoding/AudioSampleTiming.h"

#include <QtEndian>

#include <limits>

using SnapTray::Audio::ByteOrder;
using SnapTray::Audio::InputChunk;
using SnapTray::Audio::OutputChunk;
using SnapTray::Audio::Pcm16Format;
using SnapTray::Audio::Source;
using SnapTray::Audio::TimestampedPcmMixer;
using SnapTray::Audio::scaleAudioSampleRange;

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

TimestampedPcmMixer::ProcessResult pushChunk(
    TimestampedPcmMixer& mixer,
    Source source,
    const InputChunk& chunk)
{
    return mixer.push(source, chunk, qMax<qint64>(0, chunk.startTimeNs));
}

} // namespace

class tst_TimestampedPcmMixer : public QObject
{
    Q_OBJECT

private slots:
    void outputFormatIsCanonical();
    void outputChunkTimestampsPreserveFramePrecision();
    void encoderTimebaseKeepsAdjacentChunksContiguous();
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
    void rejectsFarFutureTimestampWithoutStateMutation();
    void futureTimestampBoundaryIsExact();
    void materializesOnlyBoundedSilenceForLegitimateGap();
    void peerCoveragePreventsSparseSilenceSkip();
    void zeroSilenceBudgetDoesNotLeakChunkTail();
    void repeatedSmallAdvancesUsePerCallSilenceBudget();
    void futureActivationCoverageSurvivesSparseSkip();
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

void tst_TimestampedPcmMixer::outputChunkTimestampsPreserveFramePrecision()
{
    TimestampedPcmMixer::Config preciseConfig;
    preciseConfig.microphoneEnabled = true;
    preciseConfig.systemAudioEnabled = false;
    TimestampedPcmMixer mixer(preciseConfig);
    const Pcm16Format mono = format(48000, 1);
    const QByteArray packet = monoRamp(512);

    QVector<OutputChunk> output = pushChunk(mixer,
        Source::Microphone, {packet, 0, mono}).output;
    output += pushChunk(mixer,
        Source::Microphone, {packet, nsForFrames(512, 48000), mono}).output;

    QCOMPARE(output.size(), 4);
    QCOMPARE(output.at(0).startFrame, qint64(0));
    QCOMPARE(output.at(1).startFrame, qint64(480));
    QCOMPARE(output.at(2).startFrame, qint64(512));
    QCOMPARE(output.at(3).startFrame, qint64(992));
    for (qsizetype i = 1; i < output.size(); ++i) {
        const qint64 previousFrames = output.at(i - 1).pcm.size()
            / (TimestampedPcmMixer::kOutputChannels * 2);
        QCOMPARE(output.at(i).startFrame,
                 output.at(i - 1).startFrame + previousFrames);
    }
}

void tst_TimestampedPcmMixer::encoderTimebaseKeepsAdjacentChunksContiguous()
{
    constexpr qint64 mediaFoundationUnitsPerSecond = 10000000LL;
    const auto first = scaleAudioSampleRange(
        0, 480, 48000, mediaFoundationUnitsPerSecond);
    const auto remainder = scaleAudioSampleRange(
        480, 32, 48000, mediaFoundationUnitsPerSecond);
    const auto next = scaleAudioSampleRange(
        512, 480, 48000, mediaFoundationUnitsPerSecond);

    QVERIFY(first.valid);
    QVERIFY(remainder.valid);
    QVERIFY(next.valid);
    QCOMPARE(first.start, qint64(0));
    QCOMPARE(first.duration, qint64(100000));
    QCOMPARE(remainder.start, qint64(100000));
    QCOMPARE(remainder.duration, qint64(6666));
    QCOMPARE(next.start, qint64(106666));
    QCOMPARE(first.start + first.duration, remainder.start);
    QCOMPARE(remainder.start + remainder.duration, next.start);

    QVERIFY(!scaleAudioSampleRange(
                 -1, 1, 48000, mediaFoundationUnitsPerSecond).valid);
    QVERIFY(!scaleAudioSampleRange(
                 std::numeric_limits<qint64>::max() - 1,
                 2,
                 48000,
                 mediaFoundationUnitsPerSecond).valid);
    QVERIFY(!scaleAudioSampleRange(
                 std::numeric_limits<qint64>::max(),
                 0,
                 48000,
                 mediaFoundationUnitsPerSecond).valid);
}

void tst_TimestampedPcmMixer::rejectsMalformedAndUnsupportedInput()
{
    TimestampedPcmMixer mixer(config(true, false));

    InputChunk malformed{pcm16({1}), 0, format(0, 1)};
    QCOMPARE(pushChunk(mixer, Source::Microphone, malformed).code,
             TimestampedPcmMixer::ResultCode::MalformedInput);

    InputChunk partial{QByteArray(3, '\0'), 0, format(48000, 1)};
    QCOMPARE(pushChunk(mixer, Source::Microphone, partial).code,
             TimestampedPcmMixer::ResultCode::MalformedInput);

    InputChunk bigEndian{pcm16({1}), 0, format(48000, 1)};
    bigEndian.format.byteOrder = ByteOrder::BigEndian;
    QCOMPARE(pushChunk(mixer, Source::Microphone, bigEndian).code,
             TimestampedPcmMixer::ResultCode::UnsupportedFormat);

    InputChunk planar{pcm16({1}), 0, format(48000, 1)};
    planar.format.interleaved = false;
    QCOMPARE(pushChunk(mixer, Source::Microphone, planar).code,
             TimestampedPcmMixer::ResultCode::UnsupportedFormat);

    InputChunk disabled{pcm16({1}), 0, format(48000, 1)};
    QCOMPARE(pushChunk(mixer, Source::SystemAudio, disabled).code,
             TimestampedPcmMixer::ResultCode::SourceDisabled);
}

void tst_TimestampedPcmMixer::singleSourceCanonicalizesMono()
{
    TimestampedPcmMixer mixer(config(true, false));
    const auto result = pushChunk(mixer,
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
    const auto result = pushChunk(mixer,
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
    QVector<OutputChunk> wholeOutput = pushChunk(whole,
        Source::Microphone,
        {input, 0, inputFormat}).output;
    wholeOutput += whole.flush(100000000).output;

    TimestampedPcmMixer partitioned(config(true, false));
    QVector<OutputChunk> partitionedOutput;
    const int framePartitions[] = {731, 997, 2682};
    int consumedFrames = 0;
    for (int frameCount : framePartitions) {
        const QByteArray packet = input.mid(consumedFrames * 2, frameCount * 2);
        partitionedOutput += pushChunk(partitioned,
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

    QVERIFY(pushChunk(mixer, Source::Microphone,
                      {pcm16({30000, -30000, 32767, -32768}), 0, stereo})
                .output.isEmpty());
    const auto result = pushChunk(mixer,
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

    QVector<OutputChunk> output = pushChunk(mixer,
        Source::Microphone, {mic, 0, stereo}).output;
    output += pushChunk(mixer,
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

    QVector<OutputChunk> output = pushChunk(mixer,
        Source::Microphone,
        {pcm16({1, 2, 3, 4}), 0, mono}).output;
    const auto overlap = pushChunk(mixer,
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
    const auto result = pushChunk(mixer,
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
    const auto wholeResult = pushChunk(whole,
        Source::Microphone,
        {input, 0, format(48000, 2)});

    TimestampedPcmMixer partitioned(smallCap);
    QVector<OutputChunk> partitionedOutput;
    partitionedOutput += pushChunk(partitioned,
        Source::Microphone,
        {input.left(16), 0, format(48000, 2)}).output;
    partitionedOutput += pushChunk(partitioned,
        Source::Microphone,
        {input.mid(16, 16), nsForFrames(4, 48000), format(48000, 2)}).output;
    partitionedOutput += pushChunk(partitioned,
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
    const auto result = pushChunk(mixer,
        Source::Microphone,
        {mic, 0, format(48000, 2)});

    QCOMPARE(join(result.output).size() / 4, 4);
    QCOMPARE(samples(join(result.output)),
             QList<qint16>({100, 100, 100, 100, 100, 100, 100, 100}));
    QCOMPARE(mixer.pendingFrames(Source::Microphone), qint64(2));
    QCOMPARE(mixer.stats().synthesizedGapFrames, qint64(4));
}

void tst_TimestampedPcmMixer::rejectsFarFutureTimestampWithoutStateMutation()
{
    constexpr qint64 oneHourNs = 60LL * 60LL * kNsPerSecond;
    TimestampedPcmMixer mixer(config(true, false));
    const Pcm16Format stereo = format(48000, 2);
    const InputChunk future{pcm16({100, 100}), oneHourNs, stereo};

    const auto rejected = mixer.push(Source::Microphone, future, 0);

    QCOMPARE(rejected.code, TimestampedPcmMixer::ResultCode::FutureTimestamp);
    QVERIFY(rejected.output.isEmpty());
    QCOMPARE(mixer.pendingFrames(Source::Microphone), qint64(0));
    QCOMPARE(mixer.pendingBytes(Source::Microphone), qsizetype(0));
    QCOMPARE(mixer.stats().futureTimestampPackets, qint64(1));
    QCOMPARE(mixer.stats().skippedSilenceFrames, qint64(0));

    const auto atStart = pushChunk(
        mixer, Source::Microphone, {pcm16({200, 200}), 0, stereo});
    QCOMPARE(atStart.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(atStart.output.size(), 1);
    QCOMPARE(atStart.output.first().startFrame, qint64(0));
    QCOMPARE(samples(join(atStart.output)), QList<qint16>({200, 200}));
    QCOMPARE(mixer.pendingFrames(Source::Microphone), qint64(0));
}

void tst_TimestampedPcmMixer::futureTimestampBoundaryIsExact()
{
    constexpr qint64 leadLimitFrames = 24000;
    auto boundaryConfig = config(true, false);
    boundaryConfig.maxFutureLeadFrames = leadLimitFrames;
    boundaryConfig.maxMaterializedSilenceFramesPerCall = 0;
    const Pcm16Format stereo = format(48000, 2);

    TimestampedPcmMixer atLimit(boundaryConfig);
    const auto accepted = atLimit.push(
        Source::Microphone,
        {pcm16({100, 100}), nsForFrames(leadLimitFrames, 48000), stereo},
        0);
    QCOMPARE(accepted.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(accepted.output.size(), 1);
    QCOMPARE(accepted.output.first().startFrame, leadLimitFrames);
    QCOMPARE(atLimit.stats().futureTimestampPackets, qint64(0));

    TimestampedPcmMixer pastLimit(boundaryConfig);
    const auto rejected = pastLimit.push(
        Source::Microphone,
        {pcm16({100, 100}), nsForFrames(leadLimitFrames + 1, 48000), stereo},
        0);
    QCOMPARE(rejected.code, TimestampedPcmMixer::ResultCode::FutureTimestamp);
    QCOMPARE(pastLimit.stats().futureTimestampPackets, qint64(1));

    const auto malformed = pastLimit.push(
        Source::Microphone,
        {pcm16({100, 100}), 0, stereo},
        -1);
    QCOMPARE(malformed.code, TimestampedPcmMixer::ResultCode::MalformedInput);
    QCOMPARE(pastLimit.stats().futureTimestampPackets, qint64(1));

    const auto continuous = pastLimit.push(
        Source::Microphone,
        {pcm16({200, 200}), 0, stereo},
        0);
    QCOMPARE(continuous.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(continuous.output.size(), 1);
    QCOMPARE(continuous.output.first().startFrame, qint64(0));
    QCOMPARE(samples(continuous.output.first().pcm), QList<qint16>({200, 200}));
}

void tst_TimestampedPcmMixer::materializesOnlyBoundedSilenceForLegitimateGap()
{
    constexpr qint64 oneHourNs = 60LL * 60LL * kNsPerSecond;
    constexpr qint64 oneHourFrame = 60LL * 60LL
        * TimestampedPcmMixer::kOutputSampleRate;
    constexpr qint64 packetFrames = 4;
    auto sparseConfig = config(true, false);
    sparseConfig.maxMaterializedSilenceFramesPerCall = 8;
    sparseConfig.outputChunkFrames = 8;
    TimestampedPcmMixer mixer(sparseConfig);
    const Pcm16Format stereo = format(48000, 2);

    const auto initial = pushChunk(
        mixer,
        Source::Microphone,
        {pcm16({1, 1, 2, 2, 3, 3, 4, 4}), 0, stereo});
    QCOMPARE(static_cast<qint64>(join(initial.output).size() / 4), packetFrames);

    const InputChunk afterGap{
        pcm16({10, 10, 20, 20, 30, 30, 40, 40}),
        oneHourNs,
        stereo,
    };
    const auto result = mixer.push(Source::Microphone, afterGap, oneHourNs);

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::Accepted);
    QVERIFY(!result.output.isEmpty());
    QCOMPARE(result.output.last().startFrame, oneHourFrame);
    const qint64 outputFrames = join(result.output).size() / 4;
    QVERIFY(outputFrames
            <= sparseConfig.maxMaterializedSilenceFramesPerCall + packetFrames);
    QCOMPARE(result.materializedSilenceFrames,
             sparseConfig.maxMaterializedSilenceFramesPerCall);
    QCOMPARE(samples(result.output.last().pcm),
             QList<qint16>({10, 10, 20, 20, 30, 30, 40, 40}));
    const qint64 expectedSkippedFrames = oneHourFrame
        - packetFrames
        - sparseConfig.maxMaterializedSilenceFramesPerCall;
    QCOMPARE(mixer.stats().skippedSilenceFrames, expectedSkippedFrames);

    QVector<OutputChunk> allOutput = initial.output;
    allOutput.append(result.output);
    qint64 previousEnd = 0;
    qint64 totalTimestampGaps = 0;
    for (const auto& chunk : allOutput) {
        QVERIFY(chunk.startFrame >= previousEnd);
        totalTimestampGaps += chunk.startFrame - previousEnd;
        previousEnd = chunk.startFrame + chunk.pcm.size() / 4;
    }
    QCOMPARE(totalTimestampGaps, expectedSkippedFrames);
}

void tst_TimestampedPcmMixer::peerCoveragePreventsSparseSilenceSkip()
{
    constexpr qint64 peerFrames = 101;
    auto sparseConfig = config(true, true);
    sparseConfig.maxMaterializedSilenceFramesPerCall = 8;
    sparseConfig.outputChunkFrames = 128;
    TimestampedPcmMixer mixer(sparseConfig);
    const Pcm16Format stereo = format(48000, 2);

    QByteArray peerPcm;
    for (qint64 frame = 0; frame < peerFrames; ++frame) {
        peerPcm += pcm16({10, 10});
    }
    const auto peer = pushChunk(
        mixer, Source::SystemAudio, {peerPcm, 0, stereo});
    QVERIFY(peer.output.isEmpty());

    const auto result = pushChunk(
        mixer,
        Source::Microphone,
        {pcm16({100, 100}), nsForFrames(peerFrames - 1, 48000), stereo});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::Accepted);
    QCOMPARE(join(result.output).size() / 4, peerFrames);
    QCOMPARE(result.output.first().startFrame, qint64(0));
    QCOMPARE(mixer.stats().skippedSilenceFrames, qint64(0));
    const QList<qint16> outputSamples = samples(join(result.output));
    QCOMPARE(outputSamples.at((peerFrames - 1) * 2), qint16(110));
    QCOMPARE(outputSamples.at((peerFrames - 1) * 2 + 1), qint16(110));
}

void tst_TimestampedPcmMixer::zeroSilenceBudgetDoesNotLeakChunkTail()
{
    auto sparseConfig = config(true, false);
    sparseConfig.maxMaterializedSilenceFramesPerCall = 0;
    sparseConfig.outputChunkFrames = 128;
    TimestampedPcmMixer mixer(sparseConfig);

    const auto packet = pushChunk(
        mixer,
        Source::Microphone,
        {pcm16({100, 100}), 0, format(48000, 2)});
    QCOMPARE(join(packet.output).size() / 4, qint64(1));

    const auto advanced = mixer.advanceTo(nsForFrames(100, 48000));
    QVERIFY(advanced.output.isEmpty());
    QCOMPARE(advanced.materializedSilenceFrames, qint64(0));
    QCOMPARE(mixer.stats().skippedSilenceFrames, qint64(99));
}

void tst_TimestampedPcmMixer::repeatedSmallAdvancesUsePerCallSilenceBudget()
{
    auto sparseConfig = config(true, false);
    sparseConfig.maxMaterializedSilenceFramesPerCall = 8;
    sparseConfig.outputChunkFrames = 128;
    TimestampedPcmMixer mixer(sparseConfig);

    qint64 totalFrames = 0;
    for (qint64 targetFrame : {qint64(5), qint64(10), qint64(15)}) {
        const auto advanced = mixer.advanceTo(nsForFrames(targetFrame, 48000));
        QCOMPARE(advanced.materializedSilenceFrames, qint64(5));
        QCOMPARE(join(advanced.output).size() / 4, qint64(5));
        totalFrames += advanced.materializedSilenceFrames;
    }

    QCOMPARE(totalFrames, qint64(15));
    QCOMPARE(mixer.stats().skippedSilenceFrames, qint64(0));
}

void tst_TimestampedPcmMixer::futureActivationCoverageSurvivesSparseSkip()
{
    auto sparseConfig = config(true, false);
    sparseConfig.maxSkewFrames = 0;
    sparseConfig.maxMaterializedSilenceFramesPerCall = 0;
    TimestampedPcmMixer mixer(sparseConfig);
    const qint64 activationNs = nsForFrames(100, 48000);

    mixer.setSourceEnabled(Source::SystemAudio, true, activationNs);
    const auto pending = pushChunk(
        mixer,
        Source::SystemAudio,
        {pcm16({321, 321}), activationNs, format(48000, 2)});
    QVERIFY(pending.output.isEmpty());

    const auto flushed = mixer.flush(nsForFrames(101, 48000));
    QCOMPARE(flushed.output.size(), 1);
    QCOMPARE(flushed.output.first().startFrame, qint64(100));
    QCOMPARE(samples(flushed.output.first().pcm), QList<qint16>({321, 321}));
    QCOMPARE(mixer.stats().skippedSilenceFrames, qint64(100));
}

void tst_TimestampedPcmMixer::futureSourceEnableHonorsEffectiveTime()
{
    auto futureConfig = config(true, false);
    futureConfig.maxSkewFrames = 2;
    TimestampedPcmMixer mixer(futureConfig);

    mixer.setSourceEnabled(Source::SystemAudio, true, nsForFrames(10, 48000));
    QByteArray mic;
    for (int i = 0; i < 6; ++i) mic += pcm16({100, 100});
    const auto micResult = pushChunk(mixer,
        Source::Microphone,
        {mic, 0, format(48000, 2)});

    QCOMPARE(join(micResult.output).size() / 4, 6);
    QCOMPARE(mixer.stats().synthesizedGapFrames, qint64(0));

    const auto staleSystem = pushChunk(mixer,
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
    const auto result = pushChunk(mixer,
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

    const auto result = pushChunk(mixer,
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
    pushChunk(mixer, Source::Microphone, {pcm16({100}), 0, highRate});

    for (int i = 0; i < 4; ++i) {
        const auto duplicate = pushChunk(mixer,
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
    const auto result = pushChunk(mixer,
        Source::Microphone,
        {late, 0, format(48000, 2)});

    QCOMPARE(result.code, TimestampedPcmMixer::ResultCode::TooLate);
    QVERIFY(result.output.isEmpty());
}

void tst_TimestampedPcmMixer::disablingSourceReleasesPeer()
{
    TimestampedPcmMixer mixer(config(true, true));
    const auto pending = pushChunk(mixer,
        Source::Microphone,
        {pcm16({1, 2, 3, 4}), 0, format(48000, 1)});
    QVERIFY(pending.output.isEmpty());

    const auto disabled = mixer.setSourceEnabled(
        Source::SystemAudio, false, nsForFrames(4, 48000));
    QCOMPARE(samples(join(disabled.output)),
             QList<qint16>({1, 1, 2, 2, 3, 3, 4, 4}));
    QCOMPARE(pushChunk(mixer, Source::SystemAudio,
                       {pcm16({1}), 0, format(48000, 1)}).code,
             TimestampedPcmMixer::ResultCode::SourceDisabled);
}

void tst_TimestampedPcmMixer::pauseResumeClearsCrossPauseState()
{
    TimestampedPcmMixer mixer(config(true, false));
    QVector<OutputChunk> beforePause = pushChunk(mixer,
        Source::Microphone,
        {pcm16({100, 200}), 0, format(44100, 1)}).output;
    beforePause += mixer.pause(nsForFrames(2, 44100)).output;

    QCOMPARE(pushChunk(mixer, Source::Microphone,
                       {pcm16({300}), 0, format(44100, 1)}).code,
             TimestampedPcmMixer::ResultCode::Paused);
    mixer.resume(nsForFrames(2, 44100));
    const auto afterResume = pushChunk(mixer,
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
    pushChunk(mixer, Source::Microphone, {pcm16({10}), 0, format(24000, 1)});
    const auto flushed = mixer.flush(nsForFrames(2, 48000));
    QVERIFY(!flushed.output.isEmpty());
    QCOMPARE(mixer.flush(nsForFrames(2, 48000)).code,
             TimestampedPcmMixer::ResultCode::Closed);
    QCOMPARE(pushChunk(mixer, Source::Microphone,
                       {pcm16({10}), 0, format(24000, 1)}).code,
             TimestampedPcmMixer::ResultCode::Closed);

    mixer.reset();
    QCOMPARE(pushChunk(mixer, Source::Microphone,
                       {pcm16({10}), 0, format(24000, 1)}).code,
             TimestampedPcmMixer::ResultCode::Accepted);
}

QTEST_MAIN(tst_TimestampedPcmMixer)
#include "tst_TimestampedPcmMixer.moc"
