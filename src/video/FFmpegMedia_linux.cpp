#include "FFmpegMedia.h"
#include <QFile>
#include <QThread>
#include <cmath>
extern "C" {
#include <libavutil/imgutils.h>
}
namespace SnapTray::FFmpeg {
QString errorString(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, text, sizeof(text)); return QString::fromUtf8(text);
}
void Decoder::close() {
    av_packet_free(&packet); av_frame_free(&frame); avcodec_free_context(&codec);
    avformat_close_input(&format); stream = nullptr; eof = false;
}
Decoder::~Decoder() { close(); }
bool Decoder::open(const QString& path, AVMediaType type) {
    close(); error.clear();
    int result = avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr);
    if (result >= 0) result = avformat_find_stream_info(format, nullptr);
    if (result >= 0) result = av_find_best_stream(format, type, -1, -1, nullptr, 0);
    if (result < 0) { error = errorString(result); close(); return false; }
    stream = format->streams[result];
    const auto* implementation = avcodec_find_decoder(stream->codecpar->codec_id);
    codec = implementation ? avcodec_alloc_context3(implementation) : nullptr;
    frame = av_frame_alloc(); packet = av_packet_alloc();
    if (!codec || !frame || !packet) { error = QStringLiteral("Cannot allocate media decoder."); close(); return false; }
    result = avcodec_parameters_to_context(codec, stream->codecpar);
    codec->thread_count = qBound(1, QThread::idealThreadCount(), 8);
    if (result >= 0) result = avcodec_open2(codec, implementation, nullptr);
    if (result < 0) { error = errorString(result); close(); return false; }
    return true;
}
bool Decoder::seek(qint64 ms) {
    if (!format) return false;
    const int result = av_seek_frame(format, stream->index, av_rescale_q(qMax<qint64>(0, ms), AVRational{1,1000}, stream->time_base), AVSEEK_FLAG_BACKWARD);
    if (result < 0) { error = errorString(result); return false; }
    avcodec_flush_buffers(codec); av_packet_unref(packet); av_frame_unref(frame); eof = false; error.clear(); return true;
}
bool Decoder::next() {
    if (!codec || !error.isEmpty()) return false;
    for (;;) {
        int result = avcodec_receive_frame(codec, frame);
        if (result == 0) return true;
        if (result == AVERROR_EOF) return false;
        if (result != AVERROR(EAGAIN)) { error = errorString(result); return false; }
        if (eof) return false;
        do {
            av_packet_unref(packet);
            result = av_read_frame(format, packet);
        } while (result >= 0 && packet->stream_index != stream->index);
        if (result < 0 && result != AVERROR_EOF) { error = errorString(result); return false; }
        eof = result == AVERROR_EOF;
        result = avcodec_send_packet(codec, eof ? nullptr : packet);
        if (result < 0) { error = errorString(result); return false; }
    }
}
qint64 Decoder::timeMs() const {
    const auto pts = frame->best_effort_timestamp;
    return pts == AV_NOPTS_VALUE ? -1 : av_rescale_q(pts, stream->time_base, AVRational{1,1000});
}
FrameReader::~FrameReader() { sws_freeContext(scaler); }
bool FrameReader::load(const QString& path) {
    current = {}; pending = exhausted = false; lastRequest = -1;
    return decoder.open(path, AVMEDIA_TYPE_VIDEO);
}
QSize FrameReader::videoSize() const { return decoder.codec ? QSize(decoder.codec->width, decoder.codec->height) : QSize{}; }
qint64 FrameReader::duration() const { return decoder.format && decoder.format->duration != AV_NOPTS_VALUE ? decoder.format->duration/1000 : 0; }
double FrameReader::frameRate() const {
    if (!decoder.format) return 0;
    return av_q2d(av_guess_frame_rate(decoder.format, decoder.stream, nullptr));
}
QImage FrameReader::frameAt(qint64 ms) {
    if (!decoder.codec) return {};
    if (ms < lastRequest && !allowSeeking) {
        decoder.error = QStringLiteral("Video reader requires ascending timestamps."); return {};
    }
    if (ms < lastRequest || ms > lastRequest + 2000) {
        if (!decoder.seek(ms)) return {};
        current = {}; pending = exhausted = false;
    }
    lastRequest = ms;
    while (!exhausted) {
        if (!pending) { pending = decoder.next(); if (!pending) { exhausted = true; break; } }
        const qint64 timestamp = decoder.timeMs();
        if (timestamp < 0) { decoder.error = QStringLiteral("Video frame has no presentation timestamp."); return {}; }
        if (!current.isNull() && timestamp > ms) break;
        auto* f = decoder.frame;
        if (av_image_check_size(f->width, f->height, 0, nullptr) < 0) return {};
        scaler = sws_getCachedContext(scaler, f->width, f->height, static_cast<AVPixelFormat>(f->format),
            f->width, f->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        // swscale's SIMD writers require padded/aligned output rows. A tightly
        // packed QImage (e.g. 120 pixels wide) can overrun its last allocation.
        AVFrame* converted = av_frame_alloc();
        if (!converted) return {};
        converted->format = AV_PIX_FMT_BGRA;
        converted->width = f->width; converted->height = f->height;
        if (!scaler || av_frame_get_buffer(converted, 64) < 0) {
            av_frame_free(&converted); decoder.error = QStringLiteral("Cannot allocate decoded image."); return {};
        }
        if (sws_scale(scaler, f->data, f->linesize, 0, f->height, converted->data, converted->linesize) != f->height) {
            av_frame_free(&converted); return {};
        }
        QImage image(converted->data[0], f->width, f->height, converted->linesize[0], QImage::Format_RGB32,
            [](void* allocation) { auto* frame = static_cast<AVFrame*>(allocation); av_frame_free(&frame); }, converted);
        current = std::move(image); pending = false;
    }
    return decoder.error.isEmpty() ? current : QImage{};
}
VideoFileProbe probeFile(const QString& path) {
    VideoFileProbe probe;
    AVFormatContext* f = nullptr;
    if (avformat_open_input(&f, QFile::encodeName(path).constData(), nullptr, nullptr) < 0) return probe;
    if (avformat_find_stream_info(f, nullptr) >= 0) {
        const int v = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const int a = av_find_best_stream(f, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (v >= 0) {
            const auto* c = f->streams[v]->codecpar;
            probe.videoSize = QSize(c->width, c->height);
            probe.durationMs = f->duration == AV_NOPTS_VALUE ? 0 : f->duration/1000;
            probe.frameRate = av_q2d(av_guess_frame_rate(f, f->streams[v], nullptr));
            if (c->codec_id == AV_CODEC_ID_H264) probe.videoCodec = kVideoCodecH264;
            else if (c->codec_id == AV_CODEC_ID_HEVC) probe.videoCodec = kVideoCodecHevc;
            probe.valid = !probe.videoSize.isEmpty() && probe.durationMs > 0;
        }
        if (a >= 0) {
            const auto* stream = f->streams[a]; probe.hasAudio = true;
            probe.audioStartMs = stream->start_time == AV_NOPTS_VALUE ? 0 : av_rescale_q(stream->start_time, stream->time_base, AVRational{1,1000});
            probe.audioEndMs = stream->duration == AV_NOPTS_VALUE ? probe.durationMs
                : probe.audioStartMs + av_rescale_q(stream->duration, stream->time_base, AVRational{1,1000});
        }
    }
    avformat_close_input(&f); return probe;
}
AudioReader::~AudioReader() { swr_free(&resampler); }
bool AudioReader::load(const QString& path) {
    pending.clear(); nextFrame = -1; exhausted = false; swr_free(&resampler);
    if (!decoder.open(path, AVMEDIA_TYPE_AUDIO)) return false;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    const int result = swr_alloc_set_opts2(&resampler, &layout, AV_SAMPLE_FMT_S16, 48000,
        &decoder.codec->ch_layout, decoder.codec->sample_fmt, decoder.codec->sample_rate, 0, nullptr);
    if (result < 0) return false;
#else
    resampler = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 48000,
        decoder.codec->channel_layout ? decoder.codec->channel_layout : av_get_default_channel_layout(decoder.codec->channels),
        decoder.codec->sample_fmt, decoder.codec->sample_rate, 0, nullptr);
#endif
    return resampler && swr_init(resampler) >= 0;
}
QByteArray AudioReader::read(qint64 start, int count) {
    if (!resampler || count <= 0 || count > 48000) return {};
    QByteArray output(count*4, '\0');
    if ((nextFrame >= 0 && start != nextFrame) || (nextFrame < 0 && start > 4800)) {
        if (!decoder.seek(qMax<qint64>(0,start/48-100))) return {};
        pending.clear(); exhausted = false; swr_close(resampler); swr_init(resampler);
    }
    nextFrame = start + count;
    while (!exhausted || !pending.isEmpty()) {
        if (pending.isEmpty()) {
            if (!decoder.next()) { exhausted = true; break; }
            auto* frame = decoder.frame;
            if (frame->best_effort_timestamp == AV_NOPTS_VALUE) {
                decoder.error = QStringLiteral("Audio frame has no timestamp."); return {};
            }
            // Keep sample precision: rounding every AAC packet to milliseconds
            // would repeatedly insert/drop up to 24 samples and cause clicks.
            const qint64 firstSample = av_rescale_q(frame->best_effort_timestamp,
                decoder.stream->time_base, AVRational{1,48000});
            const qint64 delay = av_rescale_rnd(swr_get_delay(resampler, decoder.codec->sample_rate),
                48000, decoder.codec->sample_rate, AV_ROUND_NEAR_INF);
            const int capacity = swr_get_out_samples(resampler, frame->nb_samples);
            pending.resize(capacity*4);
            uint8_t* buffers[] = {reinterpret_cast<uint8_t*>(pending.data())};
            const int n = swr_convert(resampler, buffers, capacity,
                const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
            if (n < 0) { decoder.error = errorString(n); return {}; }
            pending.resize(n*4); pendingStart = firstSample-delay;
            if (!n) continue;
        }
        if (pendingStart >= start+count) break;
        const qint64 end = pendingStart + pending.size()/4;
        const qint64 from = qMax(start, pendingStart), to = qMin(start+count, end);
        if (to > from) memcpy(output.data()+(from-start)*4, pending.constData()+(from-pendingStart)*4, (to-from)*4);
        if (end <= start+count) pending.clear();
        else {
            const qint64 consumed = start+count-pendingStart;
            if (consumed > 0) { pending.remove(0, consumed*4); pendingStart += consumed; }
            break;
        }
    }
    return decoder.error.isEmpty() ? output : QByteArray{};
}
}
std::unique_ptr<IVideoFrameReader> createFFmpegFrameReader() { return std::make_unique<SnapTray::FFmpeg::FrameReader>(); }
