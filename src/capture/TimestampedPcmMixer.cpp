#include "capture/TimestampedPcmMixer.h"

#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QtEndian>

#include <array>
#include <limits>

namespace SnapTray::Audio {

namespace {

constexpr qint64 kNanosecondsPerSecond = 1000000000LL;
constexpr qint64 kNanosecondsPerMillisecond = 1000000LL;
constexpr int kBytesPerPcm16Sample = 2;

qint64 scaledTimeToFrames(qint64 timeNs, int sampleRate)
{
    if (timeNs == 0) {
        return 0;
    }

    const bool negative = timeNs < 0;
    const quint64 magnitude = negative
        ? static_cast<quint64>(-(timeNs + 1)) + 1
        : static_cast<quint64>(timeNs);
    const quint64 seconds = magnitude / kNanosecondsPerSecond;
    const quint64 remainder = magnitude % kNanosecondsPerSecond;
    const quint64 frames = seconds * static_cast<quint64>(sampleRate)
        + (remainder * static_cast<quint64>(sampleRate)
           + kNanosecondsPerSecond / 2) / kNanosecondsPerSecond;
    const qint64 signedFrames = frames > static_cast<quint64>(std::numeric_limits<qint64>::max())
        ? std::numeric_limits<qint64>::max()
        : static_cast<qint64>(frames);
    return negative ? -signedFrames : signedFrames;
}

qint64 framesToNanoseconds(qint64 frames, int sampleRate)
{
    if (frames <= 0 || sampleRate <= 0) {
        return 0;
    }
    const qint64 seconds = frames / sampleRate;
    const qint64 remainder = frames % sampleRate;
    return seconds * kNanosecondsPerSecond
        + (remainder * kNanosecondsPerSecond) / sampleRate;
}

qint16 readLittleEndianSample(const char *data, qint64 sampleIndex)
{
    const auto *sample = reinterpret_cast<const uchar *>(
        data + sampleIndex * kBytesPerPcm16Sample);
    return static_cast<qint16>(qFromLittleEndian<quint16>(sample));
}

void appendLittleEndianSample(QByteArray& output, qint16 sample)
{
    const quint16 value = static_cast<quint16>(sample);
    char bytes[kBytesPerPcm16Sample];
    qToLittleEndian<quint16>(value, reinterpret_cast<uchar *>(bytes));
    output.append(bytes, kBytesPerPcm16Sample);
}

qint16 clampPcm16(qint64 sample)
{
    return static_cast<qint16>(qBound<qint64>(qint64(-32768), sample, qint64(32767)));
}

int sourceIndex(Source source)
{
    return source == Source::Microphone ? 0 : 1;
}

struct CanonicalSegment {
    qint64 startFrame = 0;
    QByteArray pcm;

    qint64 frameCount() const
    {
        return pcm.size() / (TimestampedPcmMixer::kOutputChannels
                             * kBytesPerPcm16Sample);
    }

    qint64 endFrame() const { return startFrame + frameCount(); }
};

class StreamNormalizer
{
public:
    struct Result {
        QVector<CanonicalSegment> segments;
        qint64 droppedInputFrames = 0;
    };

    Result push(const InputChunk& chunk)
    {
        Result result;
        const qint64 inputFrames = chunk.pcm.size() / chunk.format.bytesPerFrame();
        if (inputFrames <= 0) {
            return result;
        }

        if (!m_active || chunk.format != m_format) {
            if (m_active) {
                if (CanonicalSegment tail = flushSegment(); !tail.pcm.isEmpty()) {
                    result.segments.append(std::move(tail));
                }
            }
            begin(chunk.format, chunk.startTimeNs);
        }

        qint64 relativeStartFrame = scaledTimeToFrames(
            chunk.startTimeNs - m_anchorTimeNs,
            m_format.sampleRate);
        qint64 trimFrames = 0;
        if (relativeStartFrame < m_inputFramesSeen) {
            trimFrames = qMin(m_inputFramesSeen - relativeStartFrame, inputFrames);
            result.droppedInputFrames += trimFrames;
            relativeStartFrame += trimFrames;
        }

        if (trimFrames >= inputFrames) {
            return result;
        }

        if (relativeStartFrame > m_inputFramesSeen) {
            if (CanonicalSegment tail = flushSegment(); !tail.pcm.isEmpty()) {
                result.segments.append(std::move(tail));
            }
            const qint64 adjustedStartNs = chunk.startTimeNs
                + framesToNanoseconds(trimFrames, chunk.format.sampleRate);
            begin(chunk.format, adjustedStartNs);
        }

        const char *input = chunk.pcm.constData()
            + trimFrames * chunk.format.bytesPerFrame();
        const qint64 remainingFrames = inputFrames - trimFrames;
        if (CanonicalSegment segment = process(input, remainingFrames); !segment.pcm.isEmpty()) {
            result.segments.append(std::move(segment));
        }
        return result;
    }

    CanonicalSegment flushAndReset()
    {
        CanonicalSegment tail = flushSegment();
        clear();
        return tail;
    }

    void clear()
    {
        m_active = false;
        m_format = {};
        m_anchorTimeNs = 0;
        m_anchorOutputFrame = 0;
        m_inputFramesSeen = 0;
        m_nextOutputNumerator = 0;
        m_outputFramesProduced = 0;
        m_previousFrame = {0, 0};
        m_hasPreviousFrame = false;
    }

private:
    void begin(const Pcm16Format& format, qint64 startTimeNs)
    {
        clear();
        m_active = true;
        m_format = format;
        m_anchorTimeNs = startTimeNs;
        m_anchorOutputFrame = scaledTimeToFrames(
            startTimeNs,
            TimestampedPcmMixer::kOutputSampleRate);
    }

    std::array<qint16, 2> normalizedInputFrame(const char *input, qint64 frame) const
    {
        if (m_format.channels == 1) {
            const qint16 sample = readLittleEndianSample(input, frame);
            return {sample, sample};
        }
        if (m_format.channels == 2) {
            return {
                readLittleEndianSample(input, frame * 2),
                readLittleEndianSample(input, frame * 2 + 1),
            };
        }

        qint64 sum = 0;
        const qint64 firstSample = frame * m_format.channels;
        for (int channel = 0; channel < m_format.channels; ++channel) {
            sum += readLittleEndianSample(input, firstSample + channel);
        }
        const qint16 average = clampPcm16(sum / m_format.channels);
        return {average, average};
    }

    static qint16 interpolate(qint16 previous, qint16 current,
                              qint64 fractionNumerator, qint64 denominator)
    {
        qint64 value = static_cast<qint64>(previous)
                * (denominator - fractionNumerator)
            + static_cast<qint64>(current) * fractionNumerator;
        value += value >= 0 ? denominator / 2 : -(denominator / 2);
        return clampPcm16(value / denominator);
    }

    CanonicalSegment process(const char *input, qint64 frameCount)
    {
        const qint64 firstOutputFrame = m_outputFramesProduced;
        QByteArray output;
        const qint64 estimatedFrames = qMax<qint64>(
            1,
            (frameCount * TimestampedPcmMixer::kOutputSampleRate
             + m_format.sampleRate - 1) / m_format.sampleRate + 2);
        output.reserve(static_cast<qsizetype>(estimatedFrames
            * TimestampedPcmMixer::kOutputChannels * kBytesPerPcm16Sample));

        for (qint64 frame = 0; frame < frameCount; ++frame) {
            const auto currentFrame = normalizedInputFrame(input, frame);
            const qint64 inputFrameIndex = m_inputFramesSeen;

            if (!m_hasPreviousFrame) {
                appendLittleEndianSample(output, currentFrame[0]);
                appendLittleEndianSample(output, currentFrame[1]);
                ++m_outputFramesProduced;
                m_nextOutputNumerator = m_format.sampleRate;
                m_previousFrame = currentFrame;
                m_hasPreviousFrame = true;
                ++m_inputFramesSeen;
                continue;
            }

            const qint64 currentPositionNumerator = inputFrameIndex
                * TimestampedPcmMixer::kOutputSampleRate;
            const qint64 previousPositionNumerator = (inputFrameIndex - 1)
                * TimestampedPcmMixer::kOutputSampleRate;
            while (m_nextOutputNumerator <= currentPositionNumerator) {
                const qint64 fraction = m_nextOutputNumerator
                    - previousPositionNumerator;
                appendLittleEndianSample(output, interpolate(
                    m_previousFrame[0], currentFrame[0], fraction,
                    TimestampedPcmMixer::kOutputSampleRate));
                appendLittleEndianSample(output, interpolate(
                    m_previousFrame[1], currentFrame[1], fraction,
                    TimestampedPcmMixer::kOutputSampleRate));
                ++m_outputFramesProduced;
                m_nextOutputNumerator += m_format.sampleRate;
            }

            m_previousFrame = currentFrame;
            ++m_inputFramesSeen;
        }

        return {
            m_anchorOutputFrame + firstOutputFrame,
            std::move(output),
        };
    }

    CanonicalSegment flushSegment()
    {
        if (!m_active || !m_hasPreviousFrame) {
            return {};
        }

        const qint64 firstOutputFrame = m_outputFramesProduced;
        QByteArray output;
        const qint64 endPositionNumerator = m_inputFramesSeen
            * TimestampedPcmMixer::kOutputSampleRate;
        while (m_nextOutputNumerator < endPositionNumerator) {
            appendLittleEndianSample(output, m_previousFrame[0]);
            appendLittleEndianSample(output, m_previousFrame[1]);
            ++m_outputFramesProduced;
            m_nextOutputNumerator += m_format.sampleRate;
        }
        return {
            m_anchorOutputFrame + firstOutputFrame,
            std::move(output),
        };
    }

    bool m_active = false;
    Pcm16Format m_format;
    qint64 m_anchorTimeNs = 0;
    qint64 m_anchorOutputFrame = 0;
    qint64 m_inputFramesSeen = 0;
    qint64 m_nextOutputNumerator = 0;
    qint64 m_outputFramesProduced = 0;
    std::array<qint16, 2> m_previousFrame{0, 0};
    bool m_hasPreviousFrame = false;
};

struct SourceState {
    bool enabled = false;
    StreamNormalizer normalizer;
    QList<CanonicalSegment> pending;
};

qint64 segmentFrames(const QList<CanonicalSegment>& segments)
{
    qint64 total = 0;
    for (const auto& segment : segments) {
        total += segment.frameCount();
    }
    return total;
}

qsizetype segmentBytes(const QList<CanonicalSegment>& segments)
{
    qsizetype total = 0;
    for (const auto& segment : segments) {
        total += segment.pcm.size();
    }
    return total;
}

qint64 latestEndFrame(const SourceState& state)
{
    return state.pending.isEmpty() ? -1 : state.pending.constLast().endFrame();
}

void trimSegmentPrefix(CanonicalSegment& segment, qint64 frames)
{
    const qint64 trimmed = qBound<qint64>(0, frames, segment.frameCount());
    segment.pcm.remove(0, static_cast<qsizetype>(trimmed
        * TimestampedPcmMixer::kOutputChannels * kBytesPerPcm16Sample));
    segment.startFrame += trimmed;
}

} // namespace

bool Pcm16Format::isValid() const
{
    return sampleRate > 0 && sampleRate <= 384000
        && channels > 0 && channels <= 32;
}

int Pcm16Format::bytesPerFrame() const
{
    return channels * kBytesPerPcm16Sample;
}

qint64 OutputChunk::timestampNs() const
{
    return framesToNanoseconds(startFrame, TimestampedPcmMixer::kOutputSampleRate);
}

qint64 OutputChunk::timestampMs() const
{
    return timestampNs() / kNanosecondsPerMillisecond;
}

class TimestampedPcmMixer::Private
{
public:
    explicit Private(const Config& requestedConfig)
        : config(requestedConfig)
    {
        config.maxSkewFrames = qMax<qint64>(0, config.maxSkewFrames);
        config.maxPendingFramesPerSource = qMax<qint64>(1, config.maxPendingFramesPerSource);
        config.maxPendingBytesPerSource = qMax<qsizetype>(
            kOutputChannels * kBytesPerPcm16Sample,
            config.maxPendingBytesPerSource);
        config.outputChunkFrames = qMax(1, config.outputChunkFrames);
        sources[0].enabled = config.microphoneEnabled;
        sources[1].enabled = config.systemAudioEnabled;
    }

    SourceState& state(Source source) { return sources[sourceIndex(source)]; }
    const SourceState& state(Source source) const { return sources[sourceIndex(source)]; }

    void clearSource(SourceState& source)
    {
        source.normalizer.clear();
        source.pending.clear();
    }

    void resetLocked(qint64 timelineStartNs)
    {
        for (auto& source : sources) {
            clearSource(source);
        }
        sources[0].enabled = config.microphoneEnabled;
        sources[1].enabled = config.systemAudioEnabled;
        outputCursor = scaledTimeToFrames(timelineStartNs, kOutputSampleRate);
        paused = false;
        closed = false;
        statistics = {};
    }

    qint64 dropBefore(SourceState& source, qint64 frame, bool late)
    {
        qint64 dropped = 0;
        while (!source.pending.isEmpty()) {
            auto& segment = source.pending.first();
            if (segment.endFrame() <= frame) {
                dropped += segment.frameCount();
                source.pending.removeFirst();
                continue;
            }
            if (segment.startFrame < frame) {
                const qint64 prefix = frame - segment.startFrame;
                trimSegmentPrefix(segment, prefix);
                dropped += prefix;
            }
            break;
        }
        if (late) {
            statistics.lateFrames += dropped;
        }
        return dropped;
    }

    qint64 enforceCap(SourceState& source)
    {
        qint64 dropped = 0;
        const qint64 latest = latestEndFrame(source);
        const qint64 minimumFrame = latest >= 0
            ? latest - config.maxPendingFramesPerSource
            : 0;
        if (latest >= 0) {
            dropped += dropBefore(source, minimumFrame, false);
        }

        qsizetype bytes = segmentBytes(source.pending);
        while (bytes > config.maxPendingBytesPerSource && !source.pending.isEmpty()) {
            auto& segment = source.pending.first();
            const qsizetype excess = bytes - config.maxPendingBytesPerSource;
            const qint64 framesToDrop = qMin<qint64>(
                segment.frameCount(),
                (excess + kOutputChannels * kBytesPerPcm16Sample - 1)
                    / (kOutputChannels * kBytesPerPcm16Sample));
            trimSegmentPrefix(segment, framesToDrop);
            dropped += framesToDrop;
            if (segment.pcm.isEmpty()) {
                source.pending.removeFirst();
            }
            bytes = segmentBytes(source.pending);
        }

        statistics.overflowFrames += dropped;
        return dropped;
    }

    qint64 appendSegment(SourceState& source, CanonicalSegment segment)
    {
        if (segment.pcm.isEmpty()) {
            return 0;
        }

        qint64 dropped = 0;
        if (segment.startFrame < outputCursor) {
            const qint64 prefix = qMin(outputCursor - segment.startFrame,
                                       segment.frameCount());
            trimSegmentPrefix(segment, prefix);
            dropped += prefix;
            statistics.lateFrames += prefix;
        }
        if (segment.pcm.isEmpty()) {
            return dropped;
        }

        if (!source.pending.isEmpty()) {
            const qint64 overlap = source.pending.constLast().endFrame() - segment.startFrame;
            if (overlap > 0) {
                const qint64 trimmed = qMin(overlap, segment.frameCount());
                trimSegmentPrefix(segment, trimmed);
                dropped += trimmed;
                statistics.overlapFrames += trimmed;
            }
        }
        if (!segment.pcm.isEmpty()) {
            source.pending.append(std::move(segment));
        }
        dropped += enforceCap(source);
        statistics.peakPendingBytes = qMax(
            statistics.peakPendingBytes,
            segmentBytes(source.pending));
        return dropped;
    }

    qint16 sampleAt(const SourceState& source, qint64 frame, int channel,
                    bool *present) const
    {
        for (const auto& segment : source.pending) {
            if (frame < segment.startFrame) {
                break;
            }
            if (frame >= segment.endFrame()) {
                continue;
            }
            const qint64 relativeFrame = frame - segment.startFrame;
            const qint64 sampleIndex = relativeFrame * kOutputChannels + channel;
            *present = true;
            return readLittleEndianSample(segment.pcm.constData(), sampleIndex);
        }
        *present = false;
        return 0;
    }

    void consumeThrough(SourceState& source, qint64 frame)
    {
        dropBefore(source, frame, false);
    }

    int enabledSourceCount() const
    {
        return static_cast<int>(sources[0].enabled)
            + static_cast<int>(sources[1].enabled);
    }

    qint64 automaticFrontier() const
    {
        const int enabledCount = enabledSourceCount();
        if (enabledCount == 0) {
            return outputCursor;
        }

        const qint64 micEnd = sources[0].enabled ? latestEndFrame(sources[0]) : -1;
        const qint64 systemEnd = sources[1].enabled ? latestEndFrame(sources[1]) : -1;
        if (enabledCount == 1) {
            return qMax(micEnd, systemEnd);
        }

        const qint64 maximumEnd = qMax(micEnd, systemEnd);
        const qint64 minimumEnd = micEnd >= 0 && systemEnd >= 0
            ? qMin(micEnd, systemEnd)
            : outputCursor;
        return qMax(minimumEnd, maximumEnd - config.maxSkewFrames);
    }

    void drainTo(qint64 frontier, ProcessResult& result)
    {
        if (enabledSourceCount() == 0) {
            return;
        }
        frontier = qMax(frontier, outputCursor);
        while (outputCursor < frontier) {
            const qint64 frames = qMin<qint64>(
                config.outputChunkFrames,
                frontier - outputCursor);
            OutputChunk chunk;
            chunk.startFrame = outputCursor;
            chunk.pcm.reserve(static_cast<qsizetype>(
                frames * kOutputChannels * kBytesPerPcm16Sample));

            for (qint64 offset = 0; offset < frames; ++offset) {
                const qint64 frame = outputCursor + offset;
                bool micPresent[2] = {false, false};
                bool systemPresent[2] = {false, false};
                for (int channel = 0; channel < kOutputChannels; ++channel) {
                    const qint16 mic = sources[0].enabled
                        ? sampleAt(sources[0], frame, channel, &micPresent[channel])
                        : 0;
                    const qint16 system = sources[1].enabled
                        ? sampleAt(sources[1], frame, channel, &systemPresent[channel])
                        : 0;
                    appendLittleEndianSample(
                        chunk.pcm,
                        clampPcm16(static_cast<qint64>(mic) + system));
                }

                if (sources[0].enabled && !micPresent[0]) {
                    ++statistics.synthesizedGapFrames;
                }
                if (sources[1].enabled && !systemPresent[0]) {
                    ++statistics.synthesizedGapFrames;
                }
            }

            outputCursor += frames;
            consumeThrough(sources[0], outputCursor);
            consumeThrough(sources[1], outputCursor);
            result.output.append(std::move(chunk));
        }
    }

    void appendNormalizerTail(SourceState& source, ProcessResult& result)
    {
        CanonicalSegment tail = source.normalizer.flushAndReset();
        result.droppedFrames += appendSegment(source, std::move(tail));
    }

    Config config;
    SourceState sources[2];
    qint64 outputCursor = 0;
    bool paused = false;
    bool closed = false;
    Stats statistics;
    mutable QMutex mutex;
};

TimestampedPcmMixer::TimestampedPcmMixer(const Config& config)
    : d(std::make_unique<Private>(config))
{
}

TimestampedPcmMixer::~TimestampedPcmMixer() = default;

IAudioCaptureEngine::AudioFormat TimestampedPcmMixer::outputFormat()
{
    return {kOutputSampleRate, kOutputChannels, kOutputBitsPerSample};
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::push(
    Source source,
    const InputChunk& chunk)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (d->paused) {
        result.code = ResultCode::Paused;
        return result;
    }

    auto& state = d->state(source);
    if (!state.enabled) {
        result.code = ResultCode::SourceDisabled;
        return result;
    }
    if (!chunk.format.isValid() || chunk.startTimeNs < 0 || chunk.pcm.isEmpty()
        || chunk.pcm.size() % chunk.format.bytesPerFrame() != 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }
    if (!chunk.format.signedSamples || !chunk.format.interleaved
        || chunk.format.byteOrder != ByteOrder::LittleEndian) {
        result.code = ResultCode::UnsupportedFormat;
        return result;
    }

    const auto normalized = state.normalizer.push(chunk);
    const qint64 normalizedDrops = (normalized.droppedInputFrames * kOutputSampleRate
        + chunk.format.sampleRate / 2) / chunk.format.sampleRate;
    result.droppedFrames += normalizedDrops;
    d->statistics.overlapFrames += normalizedDrops;
    bool appended = false;
    for (auto segment : normalized.segments) {
        const qint64 beforeFrames = segment.frameCount();
        const qint64 dropped = d->appendSegment(state, std::move(segment));
        result.droppedFrames += dropped;
        appended = appended || dropped < beforeFrames;
    }

    d->drainTo(d->automaticFrontier(), result);
    if (result.droppedFrames > 0) {
        result.code = appended || !normalized.segments.isEmpty()
            ? ResultCode::AcceptedWithDrops
            : ResultCode::TooLate;
    }
    return result;
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::setSourceEnabled(
    Source source,
    bool enabled,
    qint64 effectiveTimeNs)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (effectiveTimeNs < 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }

    auto& state = d->state(source);
    if (state.enabled == enabled) {
        return result;
    }

    if (!enabled) {
        d->appendNormalizerTail(state, result);
        d->drainTo(scaledTimeToFrames(effectiveTimeNs, kOutputSampleRate), result);
        d->clearSource(state);
        state.enabled = false;
        d->drainTo(d->automaticFrontier(), result);
    } else {
        d->clearSource(state);
        state.enabled = true;
    }

    if (result.droppedFrames > 0) {
        result.code = ResultCode::AcceptedWithDrops;
    }
    return result;
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::advanceTo(qint64 activeTimeNs)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (d->paused) {
        result.code = ResultCode::Paused;
        return result;
    }
    if (activeTimeNs < 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }

    qint64 frontier = scaledTimeToFrames(activeTimeNs, kOutputSampleRate);
    if (d->enabledSourceCount() > 1) {
        frontier -= d->config.maxSkewFrames;
    }
    d->drainTo(frontier, result);
    return result;
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::pause(qint64 activeTimeNs)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (activeTimeNs < 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }

    d->appendNormalizerTail(d->sources[0], result);
    d->appendNormalizerTail(d->sources[1], result);
    d->drainTo(scaledTimeToFrames(activeTimeNs, kOutputSampleRate), result);
    d->clearSource(d->sources[0]);
    d->clearSource(d->sources[1]);
    d->paused = true;
    return result;
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::resume(qint64 activeTimeNs)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (activeTimeNs < 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }

    d->clearSource(d->sources[0]);
    d->clearSource(d->sources[1]);
    d->outputCursor = qMax(
        d->outputCursor,
        scaledTimeToFrames(activeTimeNs, kOutputSampleRate));
    d->paused = false;
    return result;
}

TimestampedPcmMixer::ProcessResult TimestampedPcmMixer::flush(qint64 endTimeNs)
{
    QMutexLocker locker(&d->mutex);
    ProcessResult result;
    if (d->closed) {
        result.code = ResultCode::Closed;
        return result;
    }
    if (endTimeNs < 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }

    d->appendNormalizerTail(d->sources[0], result);
    d->appendNormalizerTail(d->sources[1], result);
    d->drainTo(scaledTimeToFrames(endTimeNs, kOutputSampleRate), result);
    d->clearSource(d->sources[0]);
    d->clearSource(d->sources[1]);
    d->closed = true;
    if (result.droppedFrames > 0) {
        result.code = ResultCode::AcceptedWithDrops;
    }
    return result;
}

void TimestampedPcmMixer::reset(qint64 timelineStartNs)
{
    QMutexLocker locker(&d->mutex);
    d->resetLocked(qMax<qint64>(0, timelineStartNs));
}

TimestampedPcmMixer::Stats TimestampedPcmMixer::stats() const
{
    QMutexLocker locker(&d->mutex);
    return d->statistics;
}

qint64 TimestampedPcmMixer::pendingFrames(Source source) const
{
    QMutexLocker locker(&d->mutex);
    return segmentFrames(d->state(source).pending);
}

qsizetype TimestampedPcmMixer::pendingBytes(Source source) const
{
    QMutexLocker locker(&d->mutex);
    return segmentBytes(d->state(source).pending);
}

} // namespace SnapTray::Audio
