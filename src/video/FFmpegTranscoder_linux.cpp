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
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override {
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
        const int fps = qBound(1, qRound(source.frameRate), 240);
        QTemporaryDir temporary(QFileInfo(request.outputPath).absolutePath() + QStringLiteral("/.snaptray-export-XXXXXX"));
        if (!temporary.isValid()) return fail(QStringLiteral("Cannot create export staging directory."));
        const QString video = temporary.filePath("video.mp4");
        FFmpegEncoder encoder;
        encoder.setBitrate(request.videoBitrate > 0 ? request.videoBitrate
            : SnapTray::VideoBitrate::forQuality(crop.size(), source.frameRate, kDefaultTranscodeQuality));
        if (!encoder.start(video, crop.size(), fps)) return fail(encoder.lastError());
        FrameReader reader;
        if (!reader.load(request.inputPath)) return fail(reader.lastError());
        // Preserve actual source presentation times. Sampling a new fixed grid
        // after an unaligned trim would shift video relative to copied audio.
        qint64 sourceTime = start;
        qint64 writtenTime = -1;
        QImage lastImage;
        while (sourceTime < end) {
            const qint64 time = sourceTime-start;
            if (progress && !progress(int(time*85/(end-start)))) return fail(QStringLiteral("Export cancelled."));
            const QImage image = reader.frameAt(sourceTime);
            if (image.isNull()) return fail(reader.lastError());
            lastImage = crop == bounds ? image : image.copy(crop);
            encoder.writeFrame(lastImage, time); writtenTime = time;
            if (!encoder.isRunning()) return fail(encoder.lastError());
            const qint64 next = reader.nextTimestampMs();
            if (next < 0 || next >= end) break;
            if (next <= sourceTime) return fail(QStringLiteral("Invalid source frame timing."));
            sourceTime = next;
        }
        // Hold the final image through a source gap at the end of the trim.
        const qint64 finalTime = qMax<qint64>(0, end-start-qRound(1000.0/fps));
        if (writtenTime < finalTime) encoder.writeFrame(lastImage, finalTime);
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
        if (!verified.valid || verified.videoSize != crop.size()
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
};
}
std::unique_ptr<IVideoTranscoder> createFFmpegTranscoder() { return std::make_unique<FFmpegTranscoder>(); }
