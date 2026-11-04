#include "video/NativeEditExport.h"
#include "video/IVideoFrameReader.h"
#include "encoding/VideoBitrate.h"

#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace SnapTray {

bool needsNativeEditExport(const VideoTranscodeRequest& r)
{
    return r.frameFilter || !r.outputSize.isEmpty() || r.frameRate > 0
        || r.videoCodec == QLatin1String(kVideoCodecHevc) || !r.segments.empty()
        || !r.audioTracksVideoPath.isEmpty();
}

QByteArray NativePcmReader::read(qint64 frame, int count)
{
    constexpr int bytes = AudioTracks::kBytesPerFrame;
    if (frame < 0 || count <= 0) return {};
    QByteArray result(count * bytes, '\0');
    const qint64 end = frame + count;
    while (!m_eof) {
        if (m_packet.isEmpty() || m_start + m_packet.size() / bytes <= frame) {
            m_packet.clear();
            if (!nextPacket(m_packet, m_start)) {
                m_eof = true;
                break;
            }
            if (m_packet.size() % bytes != 0) {
                m_error = QStringLiteral("Invalid decoded PCM packet.");
                return {};
            }
            if (m_packet.isEmpty()) continue;
        }
        if (m_start >= end) break;
        const qint64 from = qMax(frame, m_start);
        const qint64 to = qMin(end, m_start + m_packet.size() / bytes);
        if (to > from) std::memcpy(result.data() + (from - frame) * bytes,
            m_packet.constData() + (from - m_start) * bytes, size_t((to - from) * bytes));
        if (m_start + m_packet.size() / bytes >= end) break;
        m_packet.clear();
    }
    return m_error.isEmpty() ? result : QByteArray{};
}

VideoTranscodeResult exportNativeEdit(const VideoTranscodeRequest& request,
    const IVideoTranscoder::ProgressCallback& progress, IVideoTranscoder& transcoder,
    NativeEditMedia& media)
{
    VideoTranscodeResult result;
    const auto fail = [&](const QString& message) {
        result.errorMessage = message.isEmpty() ? QStringLiteral("Native export failed.") : message;
        return result;
    };
    const auto source = transcoder.probe(request.inputPath);
    if (!source.valid || request.outputPath.isEmpty()) return fail(QStringLiteral("Invalid input or output."));
    if (!transcoder.supportsVideoCodec(request.videoCodec)) return fail(QStringLiteral("Unsupported video codec."));
    auto segments = request.segments;
    if (segments.empty()) segments.push_back({request.startMs,
        request.endMs < 0 ? source.durationMs : qMin(request.endMs, source.durationMs), 1.0});
    qint64 previousEnd = 0, total = 0;
    for (auto& s : segments) {
        // Match plain trim and the FFmpeg path: container duration can round
        // just below a frame-clock endpoint (e.g. 1999 ms for a 2 s clip).
        s.endMs = qMin(s.endMs, source.durationMs);
        if (s.startMs < previousEnd || s.endMs <= s.startMs
            || !std::isfinite(s.speed) || s.speed < 0.1 || s.speed > 16.0)
            return fail(QStringLiteral("Invalid or unordered timeline segments."));
        const qint64 length = s.outputLengthMs();
        if (length <= 0 || length > std::numeric_limits<qint64>::max() / 1000 - total)
            return fail(QStringLiteral("Invalid output duration."));
        total += length;
        previousEnd = s.endMs;
    }
    const TimelineMap map(segments);
    const QRect bounds(QPoint(), source.videoSize);
    QRect crop = request.cropRect.isEmpty() ? bounds : request.cropRect.intersected(bounds);
    crop.setWidth(crop.width() & ~1); crop.setHeight(crop.height() & ~1);
    if (crop.width() < 2 || crop.height() < 2) return fail(QStringLiteral("Crop rectangle is outside the video."));
    const QSize filtered = request.frameFilter && !request.filterOutputSize.isEmpty()
        ? request.filterOutputSize : crop.size();
    NativeEditFormat format;
    format.size = request.outputSize.isEmpty() ? filtered : request.outputSize.boundedTo(filtered);
    format.size = QSize(format.size.width() & ~1, format.size.height() & ~1);
    if (format.size.width() < 2 || format.size.height() < 2) return fail(QStringLiteral("Invalid output size."));
    const double sourceRate = std::isfinite(source.frameRate) && source.frameRate > 0 ? source.frameRate : 30.0;
    format.fps = qBound(1.0, request.frameRate > 0 ? qMin(double(request.frameRate), sourceRate) : sourceRate, 240.0);
    format.bitrate = request.videoBitrate > 0 ? request.videoBitrate
        : VideoBitrate::forQuality(format.size, qRound(format.fps), kDefaultTranscodeQuality);
    format.hevc = request.videoCodec == QLatin1String(kVideoCodecHevc);
    format.durationMs = map.outputDurationMs();
    std::unique_ptr<AudioTrackMixer> tracks;
    if (!request.audioTracksVideoPath.isEmpty()) {
        tracks = std::make_unique<AudioTrackMixer>(request.audioTracksVideoPath, request.audioMix);
        if (!tracks->isValid()) return fail(QStringLiteral("Cannot read the separate audio tracks."));
    }
    for (const auto& s : segments) {
        format.audio |= qFuzzyCompare(s.speed, 1.0) && (tracks || (source.hasAudio
            && qMin(s.endMs, source.audioEndMs) > qMax(s.startMs, source.audioStartMs)));
    }
    QTemporaryDir staging(QFileInfo(request.outputPath).absolutePath() + QStringLiteral("/.snaptray-export-XXXXXX"));
    if (!staging.isValid()) return fail(QStringLiteral("Cannot create export staging directory."));
    const auto stopMedia = qScopeGuard([&media] { media.abort(); });
    const QString staged = staging.filePath(QStringLiteral("output.mp4"));
    auto reader = IVideoFrameReader::create();
    if (!reader || !reader->load(request.inputPath)) return fail(reader ? reader->lastError() : QStringLiteral("Video decoder unavailable."));
    if (!media.open(request.inputPath, staged, format, format.audio && !tracks)) return fail(media.error());
    constexpr int rate = AudioTracks::kSampleRate;
    constexpr int chunk = 4800;
    qint64 audioFrame = 0;
    int audioPart = 0;
    std::atomic_bool cancelled{false};
    auto writeAudioUntil = [&](qint64 timeUs) {
        const qint64 until = timeUs * rate / 1000000;
        while (format.audio && audioFrame < until) {
            if (cancelled.load()) return false;
            while (audioPart + 1 < int(segments.size())
                && map.segmentOutputStartMs(audioPart + 1) * rate / 1000 <= audioFrame) ++audioPart;
            const qint64 partStart = map.segmentOutputStartMs(audioPart) * rate / 1000;
            const auto& part = segments[size_t(audioPart)];
            const qint64 partEnd = partStart + part.outputLengthMs() * rate / 1000;
            const int count = int(qMin<qint64>(chunk, qMin(until, partEnd) - audioFrame));
            if (count <= 0) return false;
            QByteArray pcm(count * AudioTracks::kBytesPerFrame, '\0');
            if (qFuzzyCompare(part.speed, 1.0)) {
                const qint64 sourceFrame = part.startMs * rate / 1000 + audioFrame - partStart;
                pcm = tracks ? tracks->read(sourceFrame, count) : media.audioAt(sourceFrame, count);
            }
            if (pcm.size() != count * AudioTracks::kBytesPerFrame || !media.writeAudio(pcm, audioFrame)) return false;
            audioFrame += count;
        }
        return true;
    };
    const qint64 durationUs = format.durationMs * 1000;
    const bool concurrentAudio = format.audio && media.concurrentAudio();
    bool audioSucceeded = true; // read only after the worker has joined
    std::unique_ptr<QThread> audioWorker;
    const auto stopAudio = qScopeGuard([&] {
        if (audioWorker) {
            cancelled = true;
            media.cancel();
            audioWorker->wait();
        }
    });
    if (concurrentAudio) {
        audioWorker.reset(QThread::create([&] {
            audioSucceeded = writeAudioUntil(durationUs);
            if (!audioSucceeded) {
                cancelled = true;
                media.cancel();
            }
        }));
        audioWorker->start();
    }
    // Keep source transitions on one output clock. A fixed grid alone can
    // miss a short VFR picture at a cut end. Follow native PTS, drop frames
    // against the output cadence, and fill still stretches when effects move.
    const double exactIntervalUs = 1000000.0 / format.fps;
    const qint64 intervalUs = qRound64(exactIntervalUs);
    double nextDueUs = 0.0;
    QImage pendingFrame;
    qint64 pendingTime = -1;
    auto queueFrame = [&](QImage frame, qint64 outUs) {
        if (pendingTime >= 0) {
            if (outUs <= pendingTime || !media.writeVideo(pendingFrame, pendingTime, outUs - pendingTime)
                || (!concurrentAudio && !writeAudioUntil(outUs))) return false;
        }
        pendingFrame = std::move(frame);
        pendingTime = outUs;
        return true;
    };
    auto render = [&](const QImage& cropped, qint64 sourceUs) {
        QImage frame = cropped.copy();
        if (request.frameFilter) request.frameFilter(frame, sourceUs / 1000);
        if (frame.isNull() || frame.size() != filtered) return QImage{};
        if (frame.size() != format.size) frame = frame.scaled(format.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return frame;
    };
    for (size_t index = 0; index < segments.size(); ++index) {
        const auto& part = segments[index];
        const qint64 outEnd = map.segmentOutputStartMs(int(index + 1)) * 1000;
        bool first = true;
        qint64 sourceUs = part.startMs * 1000;
        while (sourceUs < part.endMs * 1000) {
            if (cancelled.load()) return fail(media.error());
            const qint64 outUs = *map.sourceToOutputMicroseconds(sourceUs);
            if (outUs >= outEnd) break;
            if (progress && !progress(int(outUs * 90 / durationUs))) return fail(QStringLiteral("Export cancelled."));
            const QImage sourceFrame = reader->frameAtMicroseconds(sourceUs);
            if (sourceFrame.isNull()) return fail(reader->lastError());
            const qint64 nextSourceUs = reader->nextFrameTimeMicroseconds();
            if (nextSourceUs >= 0 && nextSourceUs <= sourceUs) return fail(QStringLiteral("Invalid source frame timing."));
            const qint64 nextOut = nextSourceUs < 0 || nextSourceUs >= part.endMs * 1000
                ? outEnd : qMin(outEnd, *map.sourceToOutputMicroseconds(nextSourceUs));
            constexpr qint64 kTimestampToleranceUs = 1000; // millisecond-timestamp capture sources
            // Native readers can report a VFR file's average rate. That is
            // only a cadence hint: without an explicit fps reduction, do not
            // discard the last visible picture just because a cut leaves it
            // shorter than that average interval.
            const bool terminalPicture = request.frameRate <= 0 && nextOut == outEnd;
            const bool keep = first || terminalPicture || pendingTime < 0 || nextOut - outUs >= intervalUs
                || outUs + kTimestampToleranceUs >= nextDueUs;
            if (keep && outUs > pendingTime) {
                const bool onSchedule = outUs + kTimestampToleranceUs >= nextDueUs
                    && outUs - nextDueUs <= exactIntervalUs;
                nextDueUs = onSchedule ? nextDueUs + exactIntervalUs : outUs + exactIntervalUs;
                const QImage cropped = sourceFrame.copy(crop).convertToFormat(QImage::Format_RGB32);
                QImage frame = render(cropped, sourceUs);
                if (frame.isNull()) return fail(QStringLiteral("Frame filter returned an unexpected size."));
                if (!queueFrame(std::move(frame), outUs)) return fail(media.error());
                if (request.frameFilter) {
                    for (qint64 at = outUs + intervalUs; nextOut - at >= intervalUs / 2; at += intervalUs) {
                        if (progress && !progress(int(at * 90 / durationUs))) return fail(QStringLiteral("Export cancelled."));
                        frame = render(cropped, map.outputToSourceMicroseconds(at));
                        if (frame.isNull()) return fail(QStringLiteral("Frame filter returned an unexpected size."));
                        if (!queueFrame(std::move(frame), at)) return fail(media.error());
                    }
                }
            }
            first = false;
            if (nextSourceUs < 0 || nextSourceUs >= part.endMs * 1000) break;
            sourceUs = nextSourceUs;
        }
    }
    if (pendingTime < 0 || !media.writeVideo(pendingFrame, pendingTime, durationUs - pendingTime)
        || (!concurrentAudio && !writeAudioUntil(durationUs))) return fail(media.error());
    if (audioWorker) {
        audioWorker->wait();
        audioWorker.reset();
        if (!audioSucceeded) return fail(media.error());
    }
    if (!media.finish()) return fail(media.error());
    const auto verified = transcoder.probe(staged);
    const QString codec = QLatin1String(format.hevc ? kVideoCodecHevc : kVideoCodecH264);
    if (!verified.valid || verified.videoSize != format.size || verified.videoCodec != codec
        || qAbs(verified.durationMs - format.durationMs) > qMax<qint64>(100, qRound(2000 / format.fps))
        || (format.audio && (!verified.hasAudio || verified.audioEndMs + kAudioCoverageToleranceMs < format.durationMs))) {
        qWarning() << "Edited MP4 validation failed: valid/size/codec/duration/audio/end"
                   << verified.valid << verified.videoSize << verified.videoCodec << verified.durationMs
                   << verified.hasAudio << verified.audioEndMs << "expected"
                   << format.size << codec << format.durationMs << format.audio;
        return fail(QStringLiteral("Export validation failed."));
    }
    QSaveFile output(request.outputPath);
    QFile input(staged);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return fail(QStringLiteral("Cannot open export destination."));
    while (!input.atEnd()) {
        if (progress && !progress(99)) return fail(QStringLiteral("Export cancelled."));
        const QByteArray data = input.read(1024 * 1024);
        if (data.isEmpty() || output.write(data) != data.size()) return fail(QStringLiteral("Cannot write export destination."));
    }
    if (progress && !progress(100)) return fail(QStringLiteral("Export cancelled."));
    if (!output.commit()) return fail(output.errorString());
    result.success = true;
    result.startMs = map.sourceStartMs();
    result.audioCopied = format.audio;
    return result;
}
} // namespace SnapTray
