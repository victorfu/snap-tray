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
    qint64 enabledFromFrame = 0;
    qint64 enabledFromTimeNs = 0;
    qint64 dropScaleRemainder = 0;
    int dropScaleSampleRate = 0;
    StreamNormalizer normalizer;
    QList<CanonicalSegment> pending;
};

struct AppendOutcome {
    qint64 droppedFrames = 0;
    qint64 retainedFrames = 0;
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
        config.maxFutureLeadFrames = qMax<qint64>(0, config.maxFutureLeadFrames);
        config.maxMaterializedSilenceFramesPerCall = qMax<qint64>(
            0, config.maxMaterializedSilenceFramesPerCall);
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
        source.dropScaleRemainder = 0;
        source.dropScaleSampleRate = 0;
    }

    qint64 scaleDroppedFrames(SourceState& source, qint64 inputFrames, int sampleRate)
    {
        if (inputFrames <= 0 || sampleRate <= 0) {
            return 0;
        }
        if (source.dropScaleSampleRate != sampleRate) {
            source.dropScaleSampleRate = sampleRate;
            source.dropScaleRemainder = 0;
        }
        const qint64 scaled = source.dropScaleRemainder
            + inputFrames * kOutputSampleRate;
        const qint64 outputFrames = scaled / sampleRate;
        source.dropScaleRemainder = scaled % sampleRate;
        return outputFrames;
    }

    bool sourceActiveAt(const SourceState& source, qint64 frame) const
    {
        return source.enabled && frame >= source.enabledFromFrame;
    }

    int enabledSourceCountAt(qint64 frame) const
    {
        return static_cast<int>(sourceActiveAt(sources[0], frame))
            + static_cast<int>(sourceActiveAt(sources[1], frame));
    }

    void resetLocked(qint64 timelineStartNs)
    {
        for (auto& source : sources) {
            clearSource(source);
        }
        sources[0].enabled = config.microphoneEnabled;
        sources[1].enabled = config.systemAudioEnabled;
        outputCursor = scaledTimeToFrames(timelineStartNs, kOutputSampleRate);
        for (auto& source : sources) {
            source.enabledFromFrame = outputCursor;
            source.enabledFromTimeNs = timelineStartNs;
        }
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

    AppendOutcome appendSegment(SourceState& source, CanonicalSegment segment)
    {
        AppendOutcome outcome;
        if (segment.pcm.isEmpty()) {
            return outcome;
        }

        const qint64 earliestAcceptedFrame = qMax(outputCursor, source.enabledFromFrame);
        if (segment.startFrame < earliestAcceptedFrame) {
            const qint64 prefix = qMin(earliestAcceptedFrame - segment.startFrame,
                                       segment.frameCount());
            trimSegmentPrefix(segment, prefix);
            outcome.droppedFrames += prefix;
            statistics.lateFrames += prefix;
        }
        if (segment.pcm.isEmpty()) {
            return outcome;
        }

        if (!source.pending.isEmpty()) {
            const qint64 overlap = source.pending.constLast().endFrame() - segment.startFrame;
            if (overlap > 0) {
                const qint64 trimmed = qMin(overlap, segment.frameCount());
                trimSegmentPrefix(segment, trimmed);
                outcome.droppedFrames += trimmed;
                statistics.overlapFrames += trimmed;
            }
        }
        if (!segment.pcm.isEmpty()) {
            outcome.retainedFrames = segment.frameCount();
            source.pending.append(std::move(segment));
        }
        statistics.peakPendingBytes = qMax(
            statistics.peakPendingBytes,
            segmentBytes(source.pending));
        return outcome;
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

    qint64 nextActivationAfter(qint64 frame) const
    {
        qint64 next = std::numeric_limits<qint64>::max();
        for (const auto& source : sources) {
            if (source.enabled && source.enabledFromFrame > frame) {
                next = qMin(next, source.enabledFromFrame);
            }
        }
        return next;
    }

    qint64 nextCoveredFrame(qint64 frame) const
    {
        qint64 next = std::numeric_limits<qint64>::max();
        for (const auto& source : sources) {
            if (!sourceActiveAt(source, frame)) {
                continue;
            }
            for (const auto& segment : source.pending) {
                if (segment.endFrame() <= frame) {
                    continue;
                }
                next = qMin(next, qMax(frame, segment.startFrame));
                break;
            }
        }
        return next;
    }

    qint64 coveredThrough(qint64 frame, qint64 limit)
    {
        limit = qMax(frame, limit);
        qint64 coverageEnd = frame;
        std::array<qsizetype, 2> nextIndexes = {0, 0};
        while (coverageEnd < limit) {
            bool extended = false;
            for (int sourceIndex = 0; sourceIndex < 2; ++sourceIndex) {
                const auto& source = sources[sourceIndex];
                if (!sourceActiveAt(source, frame)) {
                    continue;
                }

                auto& nextIndex = nextIndexes[sourceIndex];
                while (nextIndex < source.pending.size()) {
                    const auto& segment = source.pending.at(nextIndex);
                    if (segment.endFrame() <= frame) {
                        ++nextIndex;
                        continue;
                    }
                    if (segment.startFrame > coverageEnd) {
                        break;
                    }
                    ++nextIndex;
                    if (segment.endFrame() > coverageEnd) {
                        coverageEnd = qMin(limit, segment.endFrame());
                        extended = true;
                        if (coverageEnd == limit) {
                            return coverageEnd;
                        }
                    }
                }
            }
            if (!extended) {
                break;
            }
        }
        return coverageEnd;
    }

    qint64 automaticFrontier() const
    {
        const int enabledCount = enabledSourceCountAt(outputCursor);
        if (enabledCount == 0) {
            const qint64 nextActivation = nextActivationAfter(outputCursor);
            if (nextActivation == std::numeric_limits<qint64>::max()) {
                return outputCursor;
            }
            for (const auto& source : sources) {
                if (source.enabled
                    && source.enabledFromFrame == nextActivation
                    && latestEndFrame(source) > nextActivation) {
                    return nextActivation;
                }
            }
            return outputCursor;
        }

        const qint64 micEnd = sourceActiveAt(sources[0], outputCursor)
            ? latestEndFrame(sources[0]) : -1;
        const qint64 systemEnd = sourceActiveAt(sources[1], outputCursor)
            ? latestEndFrame(sources[1]) : -1;
        qint64 frontier = outputCursor;
        if (enabledCount == 1) {
            frontier = qMax(micEnd, systemEnd);
        } else {
            const qint64 maximumEnd = qMax(micEnd, systemEnd);
            const qint64 minimumEnd = micEnd >= 0 && systemEnd >= 0
                ? qMin(micEnd, systemEnd)
                : outputCursor;
            frontier = qMax(minimumEnd, maximumEnd - config.maxSkewFrames);
        }

        const qint64 nextActivation = nextActivationAfter(outputCursor);
        if (nextActivation != std::numeric_limits<qint64>::max()) {
            frontier = qMin(frontier, nextActivation);
        }
        return frontier;
    }

    void drainTo(qint64 frontier, ProcessResult& result)
    {
        frontier = qMax(frontier, outputCursor);
        while (outputCursor < frontier) {
            const int activeSources = enabledSourceCountAt(outputCursor);
            if (activeSources == 0) {
                const qint64 nextActivation = nextActivationAfter(outputCursor);
                outputCursor = nextActivation == std::numeric_limits<qint64>::max()
                    ? frontier
                    : qMin(frontier, nextActivation);
                continue;
            }

            const qint64 nextActivation = nextActivationAfter(outputCursor);
            const qint64 stageEnd = nextActivation == std::numeric_limits<qint64>::max()
                ? frontier
                : qMin(frontier, nextActivation);
            const qint64 coverageLimit = outputCursor + qMin<qint64>(
                config.outputChunkFrames, stageEnd - outputCursor);
            const qint64 coverageEnd = coveredThrough(outputCursor, coverageLimit);
            const bool whollyUncovered = coverageEnd <= outputCursor;
            qint64 frames = 0;
            if (whollyUncovered) {
                const qint64 silenceEnd = qMin(
                    stageEnd, nextCoveredFrame(outputCursor));
                const qint64 silenceFrames = silenceEnd - outputCursor;
                const qint64 remainingBudget = qMax<qint64>(
                    0,
                    config.maxMaterializedSilenceFramesPerCall
                        - result.materializedSilenceFrames);
                if (silenceFrames > remainingBudget) {
                    const qint64 skipTo = silenceEnd - remainingBudget;
                    statistics.skippedSilenceFrames += skipTo - outputCursor;
                    outputCursor = skipTo;
                    consumeThrough(sources[0], outputCursor);
                    consumeThrough(sources[1], outputCursor);
                    continue;
                }
                frames = qMin<qint64>(config.outputChunkFrames, silenceFrames);
            } else {
                frames = qMin<qint64>(
                    config.outputChunkFrames,
                    qMin(stageEnd, coverageEnd) - outputCursor);
            }
            OutputChunk chunk;
            chunk.startFrame = outputCursor;
            chunk.pcm.reserve(static_cast<qsizetype>(
                frames * kOutputChannels * kBytesPerPcm16Sample));

            for (qint64 offset = 0; offset < frames; ++offset) {
                const qint64 frame = outputCursor + offset;
                bool micPresent[2] = {false, false};
                bool systemPresent[2] = {false, false};
                const bool micActive = sourceActiveAt(sources[0], frame);
                const bool systemActive = sourceActiveAt(sources[1], frame);
                for (int channel = 0; channel < kOutputChannels; ++channel) {
                    const qint16 mic = micActive
                        ? sampleAt(sources[0], frame, channel, &micPresent[channel])
                        : 0;
                    const qint16 system = systemActive
                        ? sampleAt(sources[1], frame, channel, &systemPresent[channel])
                        : 0;
                    appendLittleEndianSample(
                        chunk.pcm,
                        clampPcm16(static_cast<qint64>(mic) + system));
                }

                if (micActive && !micPresent[0]) {
                    ++statistics.synthesizedGapFrames;
                }
                if (systemActive && !systemPresent[0]) {
                    ++statistics.synthesizedGapFrames;
                }
            }

            outputCursor += frames;
            if (whollyUncovered) {
                result.materializedSilenceFrames += frames;
            }
            consumeThrough(sources[0], outputCursor);
            consumeThrough(sources[1], outputCursor);
            result.output.append(std::move(chunk));
        }
    }

    void drainAutomatically(ProcessResult& result)
    {
        while (true) {
            const qint64 frontier = automaticFrontier();
            if (frontier <= outputCursor) {
                return;
            }
            drainTo(frontier, result);
        }
    }

    void advanceWithSkew(qint64 targetFrame, ProcessResult& result)
    {
        targetFrame = qMax(targetFrame, outputCursor);
        while (outputCursor < targetFrame) {
            const qint64 nextActivation = nextActivationAfter(outputCursor);
            const qint64 stageEnd = nextActivation == std::numeric_limits<qint64>::max()
                ? targetFrame
                : qMin(targetFrame, nextActivation);
            qint64 frontier = stageEnd;
            if (enabledSourceCountAt(outputCursor) > 1) {
                frontier -= config.maxSkewFrames;
            }
            if (frontier <= outputCursor) {
                return;
            }
            drainTo(frontier, result);
        }
    }

    void appendNormalizerTail(SourceState& source, ProcessResult& result)
    {
        CanonicalSegment tail = source.normalizer.flushAndReset();
        const AppendOutcome outcome = appendSegment(source, std::move(tail));
        result.droppedFrames += outcome.droppedFrames;
    }

    Config config;
    SourceState sources[2];
    qint64 outputCursor = 0;
    bool paused = false;
    bool closed = false;
    Stats statistics;
    mutable QMutex mutex;
};

TimestampedPcmMixer::TimestampedPcmMixer()
    : TimestampedPcmMixer(Config{})
{
}

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
    const InputChunk& chunk,
    qint64 activeTimeNs)
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
    if (!chunk.format.isValid() || chunk.startTimeNs < 0 || activeTimeNs < 0
        || chunk.pcm.isEmpty()
        || chunk.pcm.size() % chunk.format.bytesPerFrame() != 0) {
        result.code = ResultCode::MalformedInput;
        return result;
    }
    if (!chunk.format.signedSamples || !chunk.format.interleaved
        || chunk.format.byteOrder != ByteOrder::LittleEndian) {
        result.code = ResultCode::UnsupportedFormat;
        return result;
    }

    const qint64 chunkStartFrame = scaledTimeToFrames(
        chunk.startTimeNs, kOutputSampleRate);
    const qint64 activeFrame = scaledTimeToFrames(
        activeTimeNs, kOutputSampleRate);
    if (chunkStartFrame > activeFrame
        && chunkStartFrame - activeFrame > d->config.maxFutureLeadFrames) {
        result.code = ResultCode::FutureTimestamp;
        ++d->statistics.futureTimestampPackets;
        return result;
    }

    InputChunk effectiveChunk = chunk;
    qint64 preNormalizationDrops = 0;
    if (effectiveChunk.startTimeNs < state.enabledFromTimeNs) {
        const qint64 inputFrames = effectiveChunk.pcm.size()
            / effectiveChunk.format.bytesPerFrame();
        const qint64 chunkEndNs = effectiveChunk.startTimeNs
            + framesToNanoseconds(inputFrames, effectiveChunk.format.sampleRate);
        if (chunkEndNs <= state.enabledFromTimeNs) {
            const qint64 canonicalDrops = d->scaleDroppedFrames(
                state, inputFrames, effectiveChunk.format.sampleRate);
            result.droppedFrames = canonicalDrops;
            d->statistics.lateFrames += canonicalDrops;
            result.code = ResultCode::TooLate;
            return result;
        }

        preNormalizationDrops = qBound<qint64>(
            qint64(0),
            scaledTimeToFrames(
                state.enabledFromTimeNs - effectiveChunk.startTimeNs,
                effectiveChunk.format.sampleRate),
            inputFrames);
        effectiveChunk.pcm.remove(
            0,
            static_cast<qsizetype>(preNormalizationDrops
                * effectiveChunk.format.bytesPerFrame()));
        effectiveChunk.startTimeNs += framesToNanoseconds(
            preNormalizationDrops,
            effectiveChunk.format.sampleRate);
        if (effectiveChunk.pcm.isEmpty()) {
            const qint64 canonicalDrops = d->scaleDroppedFrames(
                state, preNormalizationDrops, effectiveChunk.format.sampleRate);
            result.droppedFrames = canonicalDrops;
            d->statistics.lateFrames += canonicalDrops;
            result.code = ResultCode::TooLate;
            return result;
        }
    }

    bool hadDrops = preNormalizationDrops > 0;
    const qint64 preNormalizationCanonicalDrops = d->scaleDroppedFrames(
        state, preNormalizationDrops, effectiveChunk.format.sampleRate);
    result.droppedFrames += preNormalizationCanonicalDrops;
    d->statistics.lateFrames += preNormalizationCanonicalDrops;

    const auto normalized = state.normalizer.push(effectiveChunk);
    hadDrops = hadDrops || normalized.droppedInputFrames > 0;
    const qint64 normalizedDrops = d->scaleDroppedFrames(
        state, normalized.droppedInputFrames, effectiveChunk.format.sampleRate);
    result.droppedFrames += normalizedDrops;
    d->statistics.overlapFrames += normalizedDrops;
    bool retainedOutput = false;
    for (auto segment : normalized.segments) {
        const AppendOutcome outcome = d->appendSegment(state, std::move(segment));
        result.droppedFrames += outcome.droppedFrames;
        hadDrops = hadDrops || outcome.droppedFrames > 0;
        retainedOutput = retainedOutput || outcome.retainedFrames > 0;
    }

    d->drainAutomatically(result);
    const qint64 overflowDrops = d->enforceCap(state);
    result.droppedFrames += overflowDrops;
    hadDrops = hadDrops || overflowDrops > 0;

    if (hadDrops) {
        const bool allProducedOutputWasLate = !normalized.segments.isEmpty()
            && !retainedOutput;
        const bool allInputWasNormalizerOverlap = normalized.segments.isEmpty()
            && normalized.droppedInputFrames > 0;
        result.code = allProducedOutputWasLate || allInputWasNormalizerOverlap
            ? ResultCode::TooLate
            : ResultCode::AcceptedWithDrops;
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
        d->drainAutomatically(result);
    } else {
        d->clearSource(state);
        state.enabled = true;
        const qint64 effectiveFrame = scaledTimeToFrames(
            effectiveTimeNs, kOutputSampleRate);
        state.enabledFromFrame = qMax(effectiveFrame, d->outputCursor);
        state.enabledFromTimeNs = state.enabledFromFrame == effectiveFrame
            ? effectiveTimeNs
            : framesToNanoseconds(state.enabledFromFrame, kOutputSampleRate);
        d->drainAutomatically(result);
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

    d->advanceWithSkew(
        scaledTimeToFrames(activeTimeNs, kOutputSampleRate),
        result);
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
