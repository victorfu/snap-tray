#include "FFmpegMedia.h"
#include "VideoTranscoderFaultInjection.h"
#include <QDebug>
#include "encoding/FFmpegEncoder.h"
#include "encoding/VideoBitrate.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <cmath>
#include <sys/stat.h>

namespace {
using namespace SnapTray::FFmpeg;
bool sameFile(const QString& a, const QString& b) {
    if (QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath()) return true;
    struct stat first{}, second{};
    return ::stat(QFile::encodeName(a).constData(), &first) == 0
        && ::stat(QFile::encodeName(b).constData(), &second) == 0
        && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}
class FFmpegTranscoder final : public IVideoTranscoder, public VideoTranscoderFaultInjection {
    VideoTranscodeFault fault = VideoTranscodeFault::None;
public:
    void setFaultForTesting(VideoTranscodeFault value) override { fault = value; }
    VideoFileProbe probe(const QString& path) override { return probeFile(path); }
    bool supportsFrameFilter() const override { return true; }
    bool supportsOutputOptions() const override { return true; }
    bool supportsSegments() const override { return true; }
    bool supportsAudioMix() const override { return true; }
    bool supportsVideoCodec(const QString& codec) const override {
        return IVideoTranscoder::supportsVideoCodec(codec)
            || (codec == QLatin1String(kVideoCodecHevc) && FFmpegEncoder::isHevcAvailable());
    }
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override {
        // Joined parts (#185) and remixed tracks (#189) both rebuild the audio.
        if (!request.segments.empty() || !request.audioTracksVideoPath.isEmpty()) return transcodeSegments(request, progress);
        VideoTranscodeResult result;
        auto fail = [&](const QString& message) { result.audioCopied = false; result.errorMessage = message; qWarning() << "FFmpeg export:" << message; return result; };
        if (sameFile(request.inputPath, request.outputPath)) return fail(QStringLiteral("Input and output paths must identify different files."));
        const auto source = probe(request.inputPath);
        const qint64 start = request.startMs, end = request.endMs < 0 ? source.durationMs : qMin(request.endMs, source.durationMs);
        if (!source.valid || start < 0 || start >= end) return fail(QStringLiteral("Invalid input or trim range."));
        const QRect bounds(QPoint(), source.videoSize);
        QRect crop = request.cropRect.isEmpty() ? bounds : request.cropRect.intersected(bounds);
        crop.setWidth(crop.width() & ~1); crop.setHeight(crop.height() & ~1);
        if (crop.width() < 2 || crop.height() < 2)
            return fail(QStringLiteral("Crop rectangle is outside the video."));
        const int sourceFps = qBound(1, qRound(source.frameRate), 240);
        const int fps = request.frameRate > 0 ? qMin(request.frameRate, sourceFps) : sourceFps;
        const bool hevc = request.videoCodec == QLatin1String(kVideoCodecHevc);
        if (!supportsVideoCodec(request.videoCodec))
            return fail(QStringLiteral("Unsupported video codec: %1").arg(request.videoCodec));
        // What the frame filter returns (it may frame the picture, #184).
        const QSize filteredSize = request.frameFilter && !request.filterOutputSize.isEmpty()
            ? request.filterOutputSize : crop.size();
        // Never larger than that, even sides.
        QSize outputSize = request.outputSize.isEmpty() ? filteredSize : request.outputSize.boundedTo(filteredSize);
        outputSize = QSize(qMax(2, outputSize.width() & ~1), qMax(2, outputSize.height() & ~1));
        // Frames closer together than this are dropped at a lower output rate.
        const qint64 outputInterval = qMax<qint64>(1, qRound(1000.0 / fps));
        const bool dropFrames = fps < sourceFps;
        const auto toOutputSize = [&outputSize](QImage& image) {
            if (image.size() != outputSize)
                image = image.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        };
        QTemporaryDir temporary(QFileInfo(request.outputPath).absolutePath() + QStringLiteral("/.snaptray-export-XXXXXX"));
        if (!temporary.isValid()) return fail(QStringLiteral("Cannot create export staging directory."));
        const QString video = temporary.filePath("video.mp4");
        FFmpegEncoder encoder;
        encoder.setBitrate(request.videoBitrate > 0 ? request.videoBitrate
            : SnapTray::VideoBitrate::forQuality(outputSize, fps, kDefaultTranscodeQuality));
        encoder.setHevc(hevc);
        if (!encoder.start(video, outputSize, fps)) return fail(encoder.lastError());
        FrameReader reader;
        if (!reader.load(request.inputPath)) return fail(reader.lastError());
        // Preserve actual source presentation times. Sampling a new fixed grid
        // after an unaligned trim would shift video relative to copied audio.
        qint64 sourceTime = start;
        qint64 writtenTime = -1;
        // The output clock: the next frame is due one exact interval after
        // the previous one was due (not after the frame kept), so a ratio
        // like 24 -> 15 keeps 5 of every 8 frames instead of every other one.
        const double exactInterval = 1000.0 / fps;
        double nextDue = 0.0;
        QImage lastImage;
        QImage lastSource;
        QImage pendingSource;
        qint64 pendingTime = -1, pendingSourceTime = 0;
        auto flush = [&](qint64 until) {
            if (pendingTime < 0) return true;
            QImage frame = pendingSource.copy();
            if (request.frameFilter)
                request.frameFilter(frame, pendingSourceTime, qMin(end, pendingSourceTime + until - pendingTime));
            toOutputSize(frame);
            encoder.writeFrame(frame, pendingTime);
            return encoder.isRunning();
        };
        auto queue = [&](const QImage& frame, qint64 outMs, qint64 sourceMs) {
            if (pendingTime >= 0 && !flush(outMs)) return false;
            pendingSource = frame;
            pendingTime = outMs;
            pendingSourceTime = sourceMs;
            writtenTime = outMs;
            return true;
        };
        while (sourceTime < end) {
            const qint64 time = sourceTime-start;
            if (progress && !progress(int(time*85/(end-start)))) return fail(QStringLiteral("Export cancelled."));
            const QImage image = reader.frameAt(sourceTime);
            if (image.isNull()) return fail(reader.lastError());
            const qint64 next = reader.nextTimestampMs();
            if (next >= 0 && next <= sourceTime) return fail(QStringLiteral("Invalid source frame timing."));
            // A lower output rate keeps a frame once a whole output interval
            // has passed since the last one written, and always the last
            // frame before a pause in the source, so the newest picture is
            // what a still stretch (and its effects) shows.
            const bool beforePause = next < 0 || next - sourceTime >= outputInterval;
            // A millisecond of slack absorbs rounded source timestamps.
            const bool keep = !dropFrames || writtenTime < 0 || beforePause || time + 1 >= nextDue;
            if (keep && dropFrames) {
                // On schedule: the next one is due an exact interval later. Kept
                // off schedule (the last before a pause) or fallen behind: the
                // clock restarts from this frame.
                const bool onSchedule = time + 1 >= nextDue && time - nextDue <= exactInterval;
                nextDue = onSchedule ? nextDue + exactInterval : time + exactInterval;
            }
            if (keep) {
                lastImage = crop == bounds ? image : image.copy(crop);
                lastSource = lastImage;
                if (!queue(lastSource, time, sourceTime)) return fail(encoder.lastError());
            }
            if (request.frameFilter && keep) {
                // Effects move on over a still picture (a pointer over a
                // static screen, a ripple): fill source gaps at the frame rate.
                const qint64 gapEnd = next < 0 ? end : qMin(next, end);
                for (qint64 t = sourceTime + outputInterval; gapEnd - t >= outputInterval / 2; t += outputInterval) {
                    // A long gap is many frames: keep reporting, and stop on Cancel.
                    if (progress && !progress(int((t - start) * 85 / (end - start))))
                        return fail(QStringLiteral("Export cancelled."));
                    if (!queue(lastSource, t - start, t)) return fail(encoder.lastError());
                }
            }
            if (next < 0 || next >= end) break;
            sourceTime = next;
        }
        // Hold the final image through a source gap at the end of the trim.
        const qint64 finalTime = qMax<qint64>(0, end-start-qRound(1000.0/fps));
        if (writtenTime < finalTime) {
            if (!queue(lastSource, finalTime, start + finalTime)) return fail(encoder.lastError());
        }
        if (!flush(end - start)) return fail(encoder.lastError());
        encoder.finish();
        if (!encoder.lastError().isEmpty()) return fail(encoder.lastError());
        const QString muxed = temporary.filePath("output.mp4");
        AVFormatContext *v = nullptr, *a = nullptr, *out = nullptr;
        AVPacket *vp = av_packet_alloc(), *ap = av_packet_alloc();
        const auto cleanup = qScopeGuard([&] {
            av_packet_free(&vp); av_packet_free(&ap);
            avformat_close_input(&v); avformat_close_input(&a);
            if (out) { if (out->pb) avio_closep(&out->pb); avformat_free_context(out); }
        });
        if (!vp || !ap) return fail(QStringLiteral("Cannot allocate export packets."));
        int code = avformat_open_input(&v, QFile::encodeName(video).constData(), nullptr, nullptr);
        if (code >= 0) code = avformat_find_stream_info(v, nullptr);
        if (code < 0) return fail(errorString(code));
        const int vi = av_find_best_stream(v, AVMEDIA_TYPE_VIDEO, -1,-1,nullptr,0);
        if (vi < 0) return fail(errorString(vi));
        int ai = -1;
        if (source.hasAudio && source.audioEndMs > start && source.audioStartMs < end) {
            code = avformat_open_input(&a, QFile::encodeName(request.inputPath).constData(), nullptr, nullptr);
            if (code >= 0) code = avformat_find_stream_info(a, nullptr);
            if (code < 0) return fail(errorString(code));
            ai = av_find_best_stream(a, AVMEDIA_TYPE_AUDIO, -1,-1,nullptr,0);
            if (ai < 0) return fail(errorString(ai));
            if (a->streams[ai]->codecpar->codec_id != AV_CODEC_ID_AAC)
                return fail(QStringLiteral("MP4 audio passthrough requires an AAC source."));
            code = av_seek_frame(a, ai, av_rescale_q(start, AVRational{1,1000}, a->streams[ai]->time_base), AVSEEK_FLAG_BACKWARD);
            if (code < 0) return fail(errorString(code));
        }
        // Read only packets whose onset belongs to the selected range. At most
        // one AAC packet is lost at an edge, within the shared 50 ms tolerance.
        auto readAudio = [&]() {
            if (ai < 0) return AVERROR_EOF;
            for (;;) {
                av_packet_unref(ap); const int r = av_read_frame(a, ap);
                if (r < 0) return r;
                if (ap->stream_index != ai) continue;
                const auto pts = ap->pts != AV_NOPTS_VALUE ? ap->pts : ap->dts;
                if (pts == AV_NOPTS_VALUE) return AVERROR_INVALIDDATA;
                const auto ms = av_rescale_q(pts, a->streams[ai]->time_base, AVRational{1,1000});
                if (ms >= end) { av_packet_unref(ap); return AVERROR_EOF; }
                if (ms >= start) return 0;
            }
        };
        int ar = readAudio();
        if (ar < 0 && ar != AVERROR_EOF) return fail(errorString(ar));
        code = avformat_alloc_output_context2(&out, nullptr, "mp4", nullptr);
        if (code < 0 || !out) return fail(QStringLiteral("Cannot create MP4 muxer."));
        auto* vs = avformat_new_stream(out, nullptr);
        if (!vs || avcodec_parameters_copy(vs->codecpar, v->streams[vi]->codecpar) < 0) return fail(QStringLiteral("Cannot copy video parameters."));
        vs->time_base = v->streams[vi]->time_base;
        AVStream* as = nullptr;
        if (ar >= 0) {
            if (fault == VideoTranscodeFault::AudioInputUnsupported) return fail(QStringLiteral("Cannot configure audio passthrough."));
            as = avformat_new_stream(out, nullptr);
            if (!as || avcodec_parameters_copy(as->codecpar, a->streams[ai]->codecpar) < 0) return fail(QStringLiteral("Cannot copy audio parameters."));
            as->time_base = a->streams[ai]->time_base;
        }
        code = avio_open(&out->pb, QFile::encodeName(muxed).constData(), AVIO_FLAG_WRITE);
        if (code >= 0) code = avformat_write_header(out, nullptr);
        if (code < 0) return fail(errorString(code));
        auto readVideo = [&] {
            for (;;) {
                av_packet_unref(vp); const int r = av_read_frame(v, vp);
                if (r < 0 || vp->stream_index == vi) return r;
            }
        };
        int vr = readVideo();
        int audioPackets = 0;
        while (vr >= 0 || ar >= 0) {
            if (progress && !progress(90)) return fail(QStringLiteral("Export cancelled."));
            const qint64 audioOffset = ai >= 0 ? av_rescale_q(start, AVRational{1,1000}, a->streams[ai]->time_base) : 0;
            const bool audio = ar >= 0 && (vr < 0 || av_compare_ts(ap->dts-audioOffset, a->streams[ai]->time_base,
                vp->dts, v->streams[vi]->time_base) < 0);
            AVPacket* packet = audio ? ap : vp;
            const auto timeBase = audio ? a->streams[ai]->time_base : v->streams[vi]->time_base;
            AVStream* target = audio ? as : vs;
            if (audio) {
                if (audioPackets == kVideoTranscodeFaultAudioSampleIndex
                    && fault == VideoTranscodeFault::AudioRetimeFailure)
                    return fail(QStringLiteral("Cannot retime audio packet."));
                if (packet->pts != AV_NOPTS_VALUE) packet->pts -= audioOffset;
                if (packet->dts != AV_NOPTS_VALUE) packet->dts -= audioOffset;
            }
            av_packet_rescale_ts(packet, timeBase, target->time_base);
            packet->stream_index = target->index; packet->pos = -1;
            if (audio && audioPackets++ == kVideoTranscodeFaultAudioSampleIndex
                && fault == VideoTranscodeFault::AudioAppendFailure)
                return fail(QStringLiteral("Cannot append audio packet."));
            code = av_interleaved_write_frame(out, packet);
            if (code < 0) return fail(errorString(code));
            if (audio) { result.audioCopied = true; ar = readAudio(); } else vr = readVideo();
        }
        if ((vr < 0 && vr != AVERROR_EOF) || (ar < 0 && ar != AVERROR_EOF)) return fail(QStringLiteral("Cannot read source media."));
        code = av_write_trailer(out);
        if (code >= 0) code = avio_closep(&out->pb);
        if (code < 0) return fail(errorString(code));
        const auto verified = probe(muxed);
        if (!verified.valid || verified.videoSize != outputSize
            || qAbs(verified.durationMs - (end-start)) > qMax(100, 2000/fps)
            || (source.hasAudio && qMin(end,source.audioEndMs)-qMax(start,source.audioStartMs) > kAudioCoverageToleranceMs && !verified.hasAudio))
            return fail(QStringLiteral("Export validation failed."));
        QSaveFile destination(request.outputPath);
        QFile staged(muxed);
        if (!staged.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return fail(QStringLiteral("Cannot open export destination."));
        while (!staged.atEnd()) {
            const auto data = staged.read(1024*1024);
            if (data.isEmpty() || destination.write(data) != data.size()) return fail(QStringLiteral("Cannot write export destination."));
            if (progress && !progress(99)) return fail(QStringLiteral("Export cancelled."));
        }
        if (progress && !progress(100)) return fail(QStringLiteral("Export cancelled."));
        if (!destination.commit()) return fail(destination.errorString());
        result.success = true; result.startMs = start; return result;
    }

private:
    // #185: the kept parts joined, each at its speed. Video frames are placed
    // at output times; audio is decoded and written again, from the source
    // for parts at normal speed and as silence for the others, so every seam
    // stays in sync (packet passthrough could not cut between AAC packets).
    VideoTranscodeResult transcodeSegments(const VideoTranscodeRequest& request, const ProgressCallback& progress) {
        VideoTranscodeResult result;
        auto fail = [&](const QString& message) { result.audioCopied = false; result.errorMessage = message; qWarning() << "FFmpeg export:" << message; return result; };
        if (sameFile(request.inputPath, request.outputPath)) return fail(QStringLiteral("Input and output paths must identify different files."));
        const auto source = probe(request.inputPath);
        if (!source.valid) return fail(QStringLiteral("Invalid input or trim range."));
        std::vector<SnapTray::TimelineSegment> segments;
        std::vector<SnapTray::TimelineSegment> requested = request.segments;
        if (requested.empty()) {
            requested.push_back({request.startMs, request.endMs < 0 ? source.durationMs : request.endMs, 1.0});
        }
        for (auto segment : requested) {
            segment.endMs = qMin(segment.endMs, source.durationMs);
            if (segment.endMs > segment.startMs && segment.startMs >= 0 && segment.speed > 0.0) segments.push_back(segment);
        }
        if (segments.empty()) return fail(QStringLiteral("Invalid input or trim range."));
        std::vector<qint64> outStarts{0};
        for (const auto& segment : segments) outStarts.push_back(outStarts.back() + segment.outputLengthMs());
        const qint64 outDuration = outStarts.back();
        if (outDuration <= 0) return fail(QStringLiteral("Invalid input or trim range."));

        const QRect bounds(QPoint(), source.videoSize);
        QRect crop = request.cropRect.isEmpty() ? bounds : request.cropRect.intersected(bounds);
        crop.setWidth(crop.width() & ~1); crop.setHeight(crop.height() & ~1);
        if (crop.width() < 2 || crop.height() < 2) return fail(QStringLiteral("Crop rectangle is outside the video."));
        const int sourceFps = qBound(1, qRound(source.frameRate), 240);
        const int fps = request.frameRate > 0 ? qMin(request.frameRate, sourceFps) : sourceFps;
        if (!supportsVideoCodec(request.videoCodec))
            return fail(QStringLiteral("Unsupported video codec: %1").arg(request.videoCodec));
        const QSize filteredSize = request.frameFilter && !request.filterOutputSize.isEmpty()
            ? request.filterOutputSize : crop.size();
        QSize outputSize = request.outputSize.isEmpty() ? filteredSize : request.outputSize.boundedTo(filteredSize);
        outputSize = QSize(qMax(2, outputSize.width() & ~1), qMax(2, outputSize.height() & ~1));
        const qint64 interval = qMax<qint64>(1, qRound(1000.0 / fps));
        const double exactOutInterval = 1000.0 / fps;

        // Audio: only when a part at normal speed reaches the source audio
        // (or, remixing separate tracks, any part at normal speed).
        std::unique_ptr<SnapTray::AudioTrackMixer> tracks;
        if (!request.audioTracksVideoPath.isEmpty()) {
            tracks = std::make_unique<SnapTray::AudioTrackMixer>(request.audioTracksVideoPath, request.audioMix);
            if (!tracks->isValid()) return fail(QStringLiteral("Cannot read the separate audio tracks."));
        }
        bool withAudio = false;
        if (tracks) {
            for (const auto& segment : segments) withAudio = withAudio || qFuzzyCompare(segment.speed, 1.0);
        } else if (source.hasAudio) {
            for (const auto& segment : segments) {
                withAudio = withAudio || (qFuzzyCompare(segment.speed, 1.0)
                    && qMin(segment.endMs, source.audioEndMs) - qMax(segment.startMs, source.audioStartMs) > 0);
            }
        }
        QTemporaryDir temporary(QFileInfo(request.outputPath).absolutePath() + QStringLiteral("/.snaptray-export-XXXXXX"));
        if (!temporary.isValid()) return fail(QStringLiteral("Cannot create export staging directory."));
        const QString staged = temporary.filePath("output.mp4");
        FFmpegEncoder encoder;
        encoder.setBitrate(request.videoBitrate > 0 ? request.videoBitrate
            : SnapTray::VideoBitrate::forQuality(outputSize, fps, kDefaultTranscodeQuality));
        encoder.setHevc(request.videoCodec == QLatin1String(kVideoCodecHevc));
        constexpr int kAudioRate = 48000, kAudioChannels = 2, kAudioBits = 16;
        constexpr int kAudioChunk = 4800; // 100 ms
        AudioReader audio;
        if (withAudio) {
            if (!tracks && !audio.load(request.inputPath))
                return fail(QStringLiteral("Cannot read source audio: %1").arg(audio.error()));
            encoder.setAudioFormat(kAudioRate, kAudioChannels, kAudioBits);
        }
        if (!encoder.start(staged, outputSize, fps)) return fail(encoder.lastError());
        if (withAudio && !encoder.isAudioEnabled()) return fail(QStringLiteral("Cannot encode audio."));

        // Audio follows the video as it is written, a part at a time.
        qint64 audioOutFrame = 0;
        const qint64 audioEndFrame = outDuration * kAudioRate / 1000;
        auto writeAudioUntil = [&](qint64 outMs) -> bool {
            if (!withAudio) return true;
            const qint64 until = qMin(audioEndFrame, outMs * kAudioRate / 1000);
            while (audioOutFrame < until) {
                // The part this output sample belongs to.
                size_t index = 0;
                while (index + 1 < segments.size() && outStarts[index + 1] * kAudioRate / 1000 <= audioOutFrame) ++index;
                const qint64 partEnd = qMin(until, outStarts[index + 1] * kAudioRate / 1000);
                const int count = int(qMin<qint64>(kAudioChunk, partEnd - audioOutFrame));
                if (count <= 0) break;
                QByteArray pcm;
                if (qFuzzyCompare(segments[index].speed, 1.0)) {
                    const qint64 sourceFrame = segments[index].startMs * kAudioRate / 1000
                        + (audioOutFrame - outStarts[index] * kAudioRate / 1000);
                    pcm = tracks ? tracks->read(sourceFrame, count) : audio.read(sourceFrame, count);
                    if (pcm.isEmpty()) return false;
                } else {
                    pcm = QByteArray(count * kAudioChannels * (kAudioBits / 8), '\0'); // a sped-up part is silent
                }
                encoder.writeAudioSamples(pcm, audioOutFrame);
                if (!encoder.isRunning()) return false;
                audioOutFrame += count;
            }
            return true;
        };

        const auto toOutputSize = [&outputSize](QImage& image) {
            if (image.size() != outputSize)
                image = image.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        };
        FrameReader reader;
        if (!reader.load(request.inputPath)) return fail(reader.lastError());
        qint64 writtenOut = -1;
        // The output clock, as on the plain path: the next frame is due one
        // exact interval after the previous one was due, so a 1.5x part of a
        // 30 fps recording keeps 30 fps, not every other frame (PR #199 review).
        double nextDueOut = 0.0;
        QImage lastImage, lastSource;
        QImage pendingSource;
        qint64 pendingOut = -1, pendingSourceMs = 0, pendingSourceEnd = 0;
        double pendingSpeed = 1.0;
        auto flush = [&](qint64 outEndMs) {
            if (pendingOut < 0) return true;
            QImage frame = pendingSource.copy();
            const qint64 sourceEndMs = qMin(pendingSourceEnd, pendingSourceMs
                + qint64(std::ceil((outEndMs - pendingOut) * pendingSpeed)));
            if (request.frameFilter) request.frameFilter(frame, pendingSourceMs, sourceEndMs);
            toOutputSize(frame);
            encoder.writeFrame(frame, pendingOut);
            return encoder.isRunning() && writeAudioUntil(outEndMs);
        };
        auto queue = [&](const QImage& frame, qint64 outMs, qint64 sourceMs,
                         const SnapTray::TimelineSegment& segment) {
            if (pendingOut >= 0 && !flush(outMs)) return false;
            pendingSource = frame;
            pendingOut = writtenOut = outMs;
            pendingSourceMs = sourceMs;
            pendingSourceEnd = segment.endMs;
            pendingSpeed = segment.speed;
            return true;
        };
        for (size_t index = 0; index < segments.size(); ++index) {
            const auto& segment = segments[index];
            const qint64 outStart = outStarts[index], outEnd = outStarts[index + 1];
            auto toOut = [&](qint64 sourceMs) {
                return outStart + qint64(std::llround(double(sourceMs - segment.startMs) / segment.speed));
            };
            bool first = true;
            qint64 sourceTime = segment.startMs;
            while (sourceTime < segment.endMs) {
                const qint64 outTime = toOut(sourceTime);
                if (progress && !progress(int(outTime * 85 / outDuration))) return fail(QStringLiteral("Export cancelled."));
                const QImage image = reader.frameAt(sourceTime);
                if (image.isNull()) return fail(reader.lastError());
                const qint64 next = reader.nextTimestampMs();
                if (next >= 0 && next <= sourceTime) return fail(QStringLiteral("Invalid source frame timing."));
                const qint64 nextOut = next < 0 || next >= segment.endMs ? outEnd : toOut(next);
                // Faster parts and lower rates drop frames closer than an
                // interval; the first frame of a part and the last before a
                // pause are always kept.
                const bool keep = first || writtenOut < 0 || nextOut - outTime >= interval
                    || outTime + 1 >= nextDueOut;
                if (keep && outTime > writtenOut) {
                    // On schedule: the next one is due an exact interval later.
                    // Kept off schedule (a part's first frame, the last before a
                    // pause) or fallen behind: the clock restarts from this frame.
                    const bool onSchedule = outTime + 1 >= nextDueOut && outTime - nextDueOut <= exactOutInterval;
                    nextDueOut = onSchedule ? nextDueOut + exactOutInterval : outTime + exactOutInterval;
                    lastImage = crop == bounds ? image : image.copy(crop);
                    lastSource = lastImage;
                    if (!queue(lastSource, outTime, sourceTime, segment))
                        return fail(encoder.lastError().isEmpty() ? audio.error() : encoder.lastError());
                    // Effects move on over a still or slowed picture.
                    if (request.frameFilter) {
                        for (qint64 o = outTime + interval; nextOut - o >= interval / 2; o += interval) {
                            if (progress && !progress(int(o * 85 / outDuration))) return fail(QStringLiteral("Export cancelled."));
                            const qint64 sourceMs = segment.startMs + qint64(std::llround(double(o - outStart) * segment.speed));
                            if (!queue(lastSource, o, sourceMs, segment))
                                return fail(encoder.lastError().isEmpty() ? audio.error() : encoder.lastError());
                        }
                    }
                }
                first = false;
                if (next < 0 || next >= segment.endMs) break;
                sourceTime = next;
            }
        }
        const qint64 finalTime = qMax<qint64>(0, outDuration - interval);
        if (writtenOut < finalTime && pendingOut >= 0) {
            const auto& segment = segments.back();
            const qint64 sourceMs = pendingSourceMs
                + qint64(std::llround((finalTime - pendingOut) * pendingSpeed));
            if (!queue(pendingSource, finalTime, sourceMs, segment)) return fail(encoder.lastError());
        }
        // The pending frame covers the final hold, including its mask interval.
        if (!flush(outDuration)) return fail(encoder.lastError().isEmpty() ? audio.error() : encoder.lastError());
        if (!writeAudioUntil(outDuration)) return fail(QStringLiteral("Cannot write audio: %1").arg(audio.error()));
        encoder.finish();
        if (!encoder.lastError().isEmpty()) return fail(encoder.lastError());
        const auto verified = probe(staged);
        if (!verified.valid || verified.videoSize != outputSize
            || qAbs(verified.durationMs - outDuration) > qMax(100, 2000 / fps) || (withAudio && !verified.hasAudio))
            return fail(QStringLiteral("Export validation failed."));
        QSaveFile destination(request.outputPath);
        QFile stagedFile(staged);
        if (!stagedFile.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) return fail(QStringLiteral("Cannot open export destination."));
        while (!stagedFile.atEnd()) {
            const auto data = stagedFile.read(1024*1024);
            if (data.isEmpty() || destination.write(data) != data.size()) return fail(QStringLiteral("Cannot write export destination."));
            if (progress && !progress(99)) return fail(QStringLiteral("Export cancelled."));
        }
        if (progress && !progress(100)) return fail(QStringLiteral("Export cancelled."));
        if (!destination.commit()) return fail(destination.errorString());
        result.success = true;
        result.audioCopied = withAudio;
        result.startMs = segments.front().startMs;
        return result;
    }
};
}
std::unique_ptr<IVideoTranscoder> createFFmpegTranscoder() { return std::make_unique<FFmpegTranscoder>(); }
