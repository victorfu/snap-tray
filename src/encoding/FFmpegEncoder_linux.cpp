#include "encoding/FFmpegEncoder.h"
#include "encoding/VideoBitrate.h"

#include <QSaveFile>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <climits>
#include <QtEndian>
#include <cerrno>
#include <cstdio>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace {
constexpr int kIoBufferSize = 32768;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMaximumFrameRate = 240;

const AVCodec* softwareEncoder()
{
    // Do not pick an arbitrary H.264 encoder: hardware wrappers may be listed
    // even when no corresponding GPU/driver exists on the host.
    for (const char* name : {"libx264", "libopenh264"}) {
        if (const AVCodec* codec = avcodec_find_encoder_by_name(name)) {
            return codec;
        }
    }
    return nullptr;
}

QString ffmpegError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

// FFmpeg 7 made the AVIO write buffer const.
#if LIBAVFORMAT_VERSION_MAJOR >= 61
int writeOutput(void* opaque, const uint8_t* data, int size)
#else
int writeOutput(void* opaque, uint8_t* data, int size)
#endif
{
    auto* file = static_cast<QSaveFile*>(opaque);
    const qint64 written = file->write(reinterpret_cast<const char*>(data), size);
    return written == size ? size : AVERROR(EIO);
}

int64_t seekOutput(void* opaque, int64_t offset, int whence)
{
    auto* file = static_cast<QSaveFile*>(opaque);
    if (whence & AVSEEK_SIZE) {
        return file->size();
    }
    whence &= ~AVSEEK_FORCE;
    qint64 base = 0;
    if (whence == SEEK_CUR) {
        base = file->pos();
    } else if (whence == SEEK_END) {
        base = file->size();
    } else if (whence != SEEK_SET) {
        return AVERROR(EINVAL);
    }
    if (offset < -base || offset > std::numeric_limits<qint64>::max() - base) {
        return AVERROR(EINVAL);
    }
    return file->seek(base + offset) ? file->pos() : AVERROR(EIO);
}
} // namespace

struct FFmpegEncoder::State
{
    std::unique_ptr<QSaveFile> output;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVIOContext* io = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* scaler = nullptr;
    AVBufferRef* device = nullptr;
    AVFrame* hardwareFrame = nullptr;
    AVCodecContext* audioCodec = nullptr;
    AVStream* audioStream = nullptr;
    AVFrame* audioFrame = nullptr;
    QByteArray audioPending;
    qint64 audioPts = 0;
    bool audioStarted = false;
    qint64 encodeNs = 0;
    SnapTray::VideoRateControl rateControl = SnapTray::VideoRateControl::Bitrate;
    QSize size;
    int frameRate = 0;
    qint64 lastPts = -1;
    qint64 originMs = -1;
};

FFmpegEncoder::FFmpegEncoder(QObject* parent) : IVideoEncoder(parent) {}
FFmpegEncoder::~FFmpegEncoder() { release(); }
bool FFmpegEncoder::isAvailable() const { return softwareEncoder() != nullptr; }
QString FFmpegEncoder::encoderName() const
{
    if (!m_encoderName.isEmpty()) return m_encoderName;
    const AVCodec* codec = softwareEncoder();
    return QStringLiteral("System FFmpeg (%1)").arg(codec ? QString::fromLatin1(codec->name)
                                                        : QStringLiteral("no software H.264 encoder"));
}
bool FFmpegEncoder::isRunning() const { return m_state != nullptr; }
QString FFmpegEncoder::lastError() const { return m_error; }
qint64 FFmpegEncoder::framesWritten() const { return m_framesWritten; }
QString FFmpegEncoder::outputPath() const { return m_outputPath; }
void FFmpegEncoder::setQuality(int quality) { m_quality = qBound(0, quality, 100); }
void FFmpegEncoder::setKeyFrameIntervalSeconds(int seconds)
{
    m_keyframeSeconds = qBound(0, seconds, 60);
}

void FFmpegEncoder::release()
{
    if (!m_state) {
        return;
    }
    auto& s = *m_state;
    if (m_framesWritten) qDebug() << "FFmpeg encoding:" << m_encoderName << m_framesWritten
        << "frames, mean conversion/encode ms" << double(s.encodeNs)/m_framesWritten/1e6;
    sws_freeContext(s.scaler);
    av_frame_free(&s.hardwareFrame);
    av_frame_free(&s.audioFrame);
    avcodec_free_context(&s.audioCodec);
    av_buffer_unref(&s.device);
    av_packet_free(&s.packet);
    av_frame_free(&s.frame);
    avcodec_free_context(&s.codec);
    avformat_free_context(s.format);
    if (s.io) {
        av_freep(&s.io->buffer);
        avio_context_free(&s.io);
    }
    // QSaveFile discards uncommitted output, preserving any previous file.
    m_state.reset();
}

void FFmpegEncoder::fail(const QString& message)
{
    m_error = message;
    release();
    qWarning() << "FFmpegEncoder:" << message;
    emit error(message);
}

bool FFmpegEncoder::start(const QString& path, const QSize& size, int frameRate)
{
    if (isRunning()) {
        return false;
    }
    m_error.clear();
    m_framesWritten = 0;
    m_outputPath = path;
    if (path.isEmpty() || size.width() < 2 || size.height() < 2
        || size.width() % 2 || size.height() % 2
        || av_image_check_size(size.width(), size.height(), 0, nullptr) < 0
        || frameRate < 1 || frameRate > kMaximumFrameRate) {
        fail(QStringLiteral("MP4 requires a valid path, even frame dimensions, and 1–240 fps."));
        return false;
    }
    const AVCodec* encoder = softwareEncoder();
    if (!encoder) {
        fail(QStringLiteral("System FFmpeg has no libx264 or libopenh264 H.264 encoder."));
        return false;
    }

    m_state = std::make_unique<State>();
    auto& s = *m_state;
    s.size = size;
    s.frameRate = frameRate;
    s.output = std::make_unique<QSaveFile>(path, this);
    if (!s.output->open(QIODevice::WriteOnly)) {
        fail(s.output->errorString());
        return false;
    }
    int result = avformat_alloc_output_context2(&s.format, nullptr, "mp4", nullptr);
    if (result < 0 || !s.format) {
        fail(QStringLiteral("Cannot allocate MP4 muxer: %1").arg(ffmpegError(result)));
        return false;
    }
    auto* buffer = static_cast<unsigned char*>(av_malloc(kIoBufferSize));
    if (buffer) {
        s.io = avio_alloc_context(buffer, kIoBufferSize, 1, s.output.get(),
                                 nullptr, writeOutput, seekOutput);
        if (!s.io) {
            av_free(buffer);
        }
    }
    s.codec = nullptr;
    s.stream = avformat_new_stream(s.format, nullptr);
    s.frame = av_frame_alloc();
    s.packet = av_packet_alloc();
    if (!s.io || !s.stream || !s.frame || !s.packet) {
        fail(QStringLiteral("Cannot allocate FFmpeg encoding buffers."));
        return false;
    }
    s.format->pb = s.io;
    s.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    auto openEncoder = [&](const AVCodec* candidate, AVBufferRef* device) {
        if (!candidate) return false;
        avcodec_free_context(&s.codec);
        s.codec = avcodec_alloc_context3(candidate);
        if (!s.codec) return false;
        s.codec->width = size.width();
        s.codec->height = size.height();
        s.codec->pix_fmt = device ? AV_PIX_FMT_VAAPI : AV_PIX_FMT_YUV420P;
        s.codec->time_base = AVRational{1, kMillisecondsPerSecond};
        s.codec->framerate = AVRational{frameRate, 1};
        s.codec->bit_rate = m_bitrate > 0 ? m_bitrate
            : SnapTray::VideoBitrate::forQuality(size, frameRate, m_quality);
        if (m_keyframeSeconds > 0) s.codec->gop_size = frameRate * m_keyframeSeconds;
        s.codec->max_b_frames = 0;
        // Bound CPU thread fan-out on high-core-count desktops.
        s.codec->thread_count = qBound(1, QThread::idealThreadCount(), 8);
        if (s.format->oformat->flags & AVFMT_GLOBALHEADER) s.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (device) {
            AVBufferRef* frames = av_hwframe_ctx_alloc(device);
            if (!frames) return false;
            auto* context = reinterpret_cast<AVHWFramesContext*>(frames->data);
            context->format = AV_PIX_FMT_VAAPI;
            context->sw_format = AV_PIX_FMT_NV12;
            context->width = size.width(); context->height = size.height();
            context->initial_pool_size = 8;
            if (av_hwframe_ctx_init(frames) < 0) { av_buffer_unref(&frames); return false; }
            s.codec->hw_frames_ctx = frames;
        }
        AVDictionary* options = nullptr;
        if (QByteArray(candidate->name) == "libx264") {
            av_dict_set(&options, "preset", "ultrafast", 0);
            av_dict_set(&options, "tune", "zerolatency", 0);
            if (m_rateControl == SnapTray::VideoRateControl::ConstantQuality) {
                const QByteArray crf = QByteArray::number(35 - m_quality * 29 / 100);
                av_dict_set(&options, "crf", crf.constData(), 0);
                s.codec->rc_max_rate = s.codec->bit_rate;
                s.codec->rc_buffer_size = qMin<int64_t>(s.codec->bit_rate * 2, INT_MAX);
            }
        }
        const int opened = avcodec_open2(s.codec, candidate, &options);
        av_dict_free(&options);
        if (opened < 0) return false;
        m_encoderName = QStringLiteral("System FFmpeg (%1)").arg(QString::fromLatin1(candidate->name));
        s.rateControl = QByteArray(candidate->name) == "libx264" ? m_rateControl : SnapTray::VideoRateControl::Bitrate;
        return true;
    };
    bool opened = false;
    const QByteArray preference = qgetenv("SNAPTRAY_FFMPEG_ENCODER");
    if (preference != "software") {
        if (preference.isEmpty() || preference == "h264_vaapi") {
            const auto nodes = QDir(QStringLiteral("/dev/dri")).entryList({QStringLiteral("renderD*")}, QDir::System | QDir::Files);
            for (const auto& node : nodes) {
                const QByteArray path = QFile::encodeName(QStringLiteral("/dev/dri/") + node);
                if (av_hwdevice_ctx_create(&s.device, AV_HWDEVICE_TYPE_VAAPI, path.constData(), nullptr, 0) >= 0
                    && openEncoder(avcodec_find_encoder_by_name("h264_vaapi"), s.device)) { opened = true; break; }
                av_buffer_unref(&s.device);
            }
        }
        if (!opened && (preference.isEmpty() || preference == "h264_nvenc"))
            opened = openEncoder(avcodec_find_encoder_by_name("h264_nvenc"), nullptr);
    }
    if (!opened) opened = openEncoder(encoder, nullptr);
    if (!opened) { fail(QStringLiteral("Cannot initialize a hardware or software H.264 encoder.")); return false; }
    s.frame->format = s.device ? AV_PIX_FMT_NV12 : s.codec->pix_fmt;
    if (s.device) {
        s.hardwareFrame = av_frame_alloc();
        if (!s.hardwareFrame) { fail(QStringLiteral("Cannot allocate GPU frame.")); return false; }
    }
    if (m_audioRate) {
        const AVCodec* aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
        s.audioCodec = aac ? avcodec_alloc_context3(aac) : nullptr;
        s.audioFrame = av_frame_alloc();
        s.audioStream = avformat_new_stream(s.format, nullptr);
        if (!s.audioCodec || !s.audioFrame || !s.audioStream) { fail(QStringLiteral("Cannot allocate AAC encoder.")); return false; }
        s.audioCodec->sample_rate = m_audioRate;
        s.audioCodec->sample_fmt = AV_SAMPLE_FMT_FLTP;
        s.audioCodec->time_base = AVRational{1, m_audioRate};
        s.audioCodec->bit_rate = 192000;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_default(&s.audioCodec->ch_layout, m_audioChannels);
#else
        s.audioCodec->channels = m_audioChannels;
        s.audioCodec->channel_layout = av_get_default_channel_layout(m_audioChannels);
#endif
        s.audioCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(s.audioCodec, aac, nullptr) < 0) { fail(QStringLiteral("Cannot initialize AAC encoder.")); return false; }
        s.audioStream->time_base = s.audioCodec->time_base;
        if (avcodec_parameters_from_context(s.audioStream->codecpar, s.audioCodec) < 0) { fail(QStringLiteral("Cannot configure AAC stream.")); return false; }
        s.audioFrame->format = s.audioCodec->sample_fmt;
        s.audioFrame->sample_rate = m_audioRate;
        s.audioFrame->nb_samples = s.audioCodec->frame_size;
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_copy(&s.audioFrame->ch_layout, &s.audioCodec->ch_layout);
#else
        s.audioFrame->channel_layout = s.audioCodec->channel_layout;
        s.audioFrame->channels = m_audioChannels;
#endif
        if (av_frame_get_buffer(s.audioFrame, 0) < 0) { fail(QStringLiteral("Cannot allocate AAC frame.")); return false; }
    }
    s.stream->time_base = s.codec->time_base;
    s.stream->avg_frame_rate = s.codec->framerate;
    result = avcodec_parameters_from_context(s.stream->codecpar, s.codec);
    if (result >= 0) {
        result = avformat_write_header(s.format, nullptr);
    }
    s.frame->width = size.width();
    s.frame->height = size.height();
    if (result >= 0) {
        result = av_frame_get_buffer(s.frame, 0);
    }
    if (result < 0) {
        fail(QStringLiteral("Cannot initialize MP4 output: %1").arg(ffmpegError(result)));
        return false;
    }
    return true;
}

bool FFmpegEncoder::drainPackets(bool audio)
{
    auto& s = *m_state;
    int result = 0;
    auto* codec = audio ? s.audioCodec : s.codec;
    auto* stream = audio ? s.audioStream : s.stream;
    while ((result = avcodec_receive_packet(codec, s.packet)) >= 0) {
        if (!audio && s.packet->duration <= 0) {
            s.packet->duration = av_rescale_q(1, AVRational{1, s.frameRate}, s.codec->time_base);
        }
        av_packet_rescale_ts(s.packet, codec->time_base, stream->time_base);
        s.packet->stream_index = stream->index;
        result = av_interleaved_write_frame(s.format, s.packet);
        av_packet_unref(s.packet);
        if (result < 0) {
            fail(QStringLiteral("Cannot write MP4 packet: %1").arg(ffmpegError(result)));
            return false;
        }
    }
    if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
        fail(QStringLiteral("H.264 encoding failed: %1").arg(ffmpegError(result)));
        return false;
    }
    return true;
}

void FFmpegEncoder::writeFrame(const QImage& image, qint64 timestampMs)
{
    if (!m_state) {
        return;
    }
    auto& s = *m_state;
    if (image.isNull() || image.size() != s.size) {
        fail(QStringLiteral("Captured frame dimensions changed during recording."));
        return;
    }
    QElapsedTimer timer; timer.start();
    // X11 delivers native BGRA. Feed it directly to swscale instead of making
    // another full-sized RGBA copy on every 4K frame.
    const bool bgra = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32;
    const QImage rgba = bgra ? image : image.convertToFormat(QImage::Format_RGBA8888);
    s.scaler = sws_getCachedContext(s.scaler, s.size.width(), s.size.height(), bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA,
                                   s.size.width(), s.size.height(), static_cast<AVPixelFormat>(s.frame->format),
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
    int result = av_frame_make_writable(s.frame);
    if (rgba.isNull() || !s.scaler || result < 0) {
        fail(QStringLiteral("Cannot prepare video frame buffers."));
        return;
    }
    const uint8_t* planes[] = {rgba.constBits(), nullptr, nullptr, nullptr};
    const int strides[] = {static_cast<int>(rgba.bytesPerLine()), 0, 0, 0};
    if (sws_scale(s.scaler, planes, strides, 0, s.size.height(), s.frame->data,
                  s.frame->linesize) != s.size.height()) {
        fail(QStringLiteral("Cannot convert captured frame to YUV420P."));
        return;
    }
    // Calculate each nominal step from the rational frame clock rather than
    // accumulating a rounded 33 ms interval (which drifts at 30 fps).
    const AVRational frameTimeBase{1, s.frameRate};
    const qint64 nominalPts = av_rescale_q(m_framesWritten, frameTimeBase, s.codec->time_base);
    const qint64 previousNominalPts = av_rescale_q(qMax<qint64>(0, m_framesWritten - 1),
                                                   frameTimeBase, s.codec->time_base);
    const qint64 step = qMax<qint64>(1, nominalPts - previousNominalPts);
    qint64 pts = s.lastPts < 0 ? 0 : s.lastPts + step;
    if (timestampMs >= 0) {
        if (s.originMs < 0) {
            s.originMs = timestampMs;
        }
        pts = qMax(s.lastPts + 1, timestampMs - (s.audioCodec ? 0 : s.originMs));
    }
    s.frame->pts = pts;
    AVFrame* submitted = s.frame;
    if (s.device) {
        av_frame_unref(s.hardwareFrame);
        result = av_hwframe_get_buffer(s.codec->hw_frames_ctx, s.hardwareFrame, 0);
        if (result >= 0) result = av_hwframe_transfer_data(s.hardwareFrame, s.frame, 0);
        if (result < 0) { fail(QStringLiteral("Cannot upload frame to GPU: %1").arg(ffmpegError(result))); return; }
        s.hardwareFrame->pts = pts; submitted = s.hardwareFrame;
    }
    result = avcodec_send_frame(s.codec, submitted);
    if (result < 0) {
        fail(QStringLiteral("Cannot submit video frame: %1").arg(ffmpegError(result)));
        return;
    }
    s.lastPts = pts;
    if (drainPackets()) {
        s.encodeNs += timer.nsecsElapsed();
        ++m_framesWritten;
        emit progress(m_framesWritten);
    }
}

void FFmpegEncoder::finish()
{
    if (!m_state) {
        emit finished(false, m_outputPath);
        return;
    }
    if (m_framesWritten == 0) {
        fail(QStringLiteral("No frames were recorded."));
        emit finished(false, m_outputPath);
        return;
    }
    if (m_state->audioCodec) {
        if (!encodeAudio(true)) { emit finished(false, m_outputPath); return; }
        const int sent = avcodec_send_frame(m_state->audioCodec, nullptr);
        if (sent < 0 || !drainPackets(true)) {
            if (m_state) fail(QStringLiteral("Cannot flush AAC encoder."));
            emit finished(false, m_outputPath); return;
        }
    }
    int result = avcodec_send_frame(m_state->codec, nullptr);
    if (result < 0) {
        fail(QStringLiteral("Cannot flush H.264 encoder: %1").arg(ffmpegError(result)));
    } else if (drainPackets()) {
        result = av_write_trailer(m_state->format);
        avio_flush(m_state->io);
        if (result < 0 || m_state->io->error < 0) {
            fail(QStringLiteral("Cannot finalize MP4 output."));
        } else if (!m_state->output->commit()) {
            fail(m_state->output->errorString());
        }
    }
    const bool success = m_state != nullptr;
    release();
    emit finished(success, m_outputPath);
}

void FFmpegEncoder::abort() { release(); }

void FFmpegEncoder::setRateControl(SnapTray::VideoRateControl mode, int quality) {
    m_rateControl = mode; setQuality(quality);
}
SnapTray::VideoRateControl FFmpegEncoder::effectiveRateControl() const {
    return m_state ? m_state->rateControl : SnapTray::VideoRateControl::Bitrate;
}
void FFmpegEncoder::setAudioFormat(int rate, int channels, int bits) {
    if (isRunning()) return;
    m_audioRate = (rate == 48000 && (channels == 1 || channels == 2) && bits == 16) ? rate : 0;
    m_audioChannels = m_audioRate ? channels : 0;
}
bool FFmpegEncoder::isAudioSupported() const { return avcodec_find_encoder(AV_CODEC_ID_AAC); }
bool FFmpegEncoder::isAudioEnabled() const { return m_state && m_state->audioCodec; }
bool FFmpegEncoder::encodeAudio(bool partial) {
    auto& s = *m_state;
    const int bytes = s.audioCodec->frame_size * m_audioChannels * 2;
    while (s.audioPending.size() >= bytes || (partial && !s.audioPending.isEmpty())) {
        const int used = qMin(bytes, int(s.audioPending.size()));
        if (av_frame_make_writable(s.audioFrame) < 0) { fail(QStringLiteral("Cannot prepare AAC frame.")); return false; }
        for (int channel = 0; channel < m_audioChannels; ++channel) {
            auto* samples = reinterpret_cast<float*>(s.audioFrame->data[channel]);
            for (int i = 0; i < s.audioCodec->frame_size; ++i) {
                const int offset = (i * m_audioChannels + channel) * 2;
                samples[i] = offset + 2 <= used ? qFromLittleEndian<qint16>(s.audioPending.constData()+offset)/32768.0f : 0.0f;
            }
        }
        s.audioFrame->pts = s.audioPts;
        s.audioPts += s.audioCodec->frame_size;
        s.audioPending.remove(0, used);
        if (avcodec_send_frame(s.audioCodec, s.audioFrame) < 0) { fail(QStringLiteral("Cannot encode AAC samples.")); return false; }
        if (!drainPackets(true)) return false;
    }
    return true;
}
void FFmpegEncoder::writeAudioSamples(const QByteArray& pcm, qint64 startFrame) {
    if (!isAudioEnabled() || pcm.isEmpty()) return;
    const int stride = m_audioChannels*2;
    if (startFrame < 0 || pcm.size()%stride) { fail(QStringLiteral("Invalid PCM sample range.")); return; }
    auto& s = *m_state;
    if (!s.audioStarted) { s.audioPts = startFrame; s.audioStarted = true; }
    qint64 expected = s.audioPts + s.audioPending.size()/stride;
    if (startFrame > expected) {
        // Preserve discontinuities without allocating unbounded silence.
        if (startFrame - expected > m_audioRate) {
            if (!encodeAudio(true)) return;
            s.audioPts = qMax(s.audioPts, startFrame);
        } else {
            s.audioPending.append(QByteArray((startFrame-expected)*stride, '\0'));
        }
    }
    expected = s.audioPts + s.audioPending.size()/stride;
    const qint64 skip = qMin<qint64>(pcm.size()/stride, qMax<qint64>(0, expected-startFrame));
    // Keep the staging buffer bounded even for large offline input chunks.
    for (qsizetype offset = skip*stride; offset < pcm.size();) {
        const qsizetype count = qMin<qsizetype>(4096*stride, pcm.size()-offset);
        s.audioPending.append(pcm.constData()+offset, count); offset += count;
        if (!encodeAudio(false)) return;
    }
}
