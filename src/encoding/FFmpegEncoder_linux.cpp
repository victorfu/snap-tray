#include "encoding/FFmpegEncoder.h"
#include "encoding/VideoBitrate.h"

#include <QSaveFile>
#include <QDebug>
#include <cerrno>
#include <cstdio>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
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
    sws_freeContext(s.scaler);
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
    s.codec = avcodec_alloc_context3(encoder);
    s.stream = avformat_new_stream(s.format, nullptr);
    s.frame = av_frame_alloc();
    s.packet = av_packet_alloc();
    if (!s.io || !s.codec || !s.stream || !s.frame || !s.packet) {
        fail(QStringLiteral("Cannot allocate FFmpeg encoding buffers."));
        return false;
    }
    s.format->pb = s.io;
    s.format->flags |= AVFMT_FLAG_CUSTOM_IO;
    s.codec->width = size.width();
    s.codec->height = size.height();
    s.codec->pix_fmt = AV_PIX_FMT_YUV420P;
    s.codec->time_base = AVRational{1, kMillisecondsPerSecond};
    s.codec->framerate = AVRational{frameRate, 1};
    s.codec->bit_rate = SnapTray::VideoBitrate::forQuality(size, frameRate, m_quality);
    if (m_keyframeSeconds > 0) {
        s.codec->gop_size = frameRate * m_keyframeSeconds;
    }
    s.codec->max_b_frames = 0;
    if (s.format->oformat->flags & AVFMT_GLOBALHEADER) {
        s.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary* options = nullptr;
    if (QByteArray(encoder->name) == "libx264") {
        av_dict_set(&options, "preset", "veryfast", 0);
        av_dict_set(&options, "tune", "zerolatency", 0);
    }
    result = avcodec_open2(s.codec, encoder, &options);
    av_dict_free(&options);
    if (result < 0) {
        fail(QStringLiteral("Cannot open H.264 encoder: %1").arg(ffmpegError(result)));
        return false;
    }
    s.stream->time_base = s.codec->time_base;
    s.stream->avg_frame_rate = s.codec->framerate;
    result = avcodec_parameters_from_context(s.stream->codecpar, s.codec);
    if (result >= 0) {
        result = avformat_write_header(s.format, nullptr);
    }
    s.frame->format = s.codec->pix_fmt;
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

bool FFmpegEncoder::drainPackets()
{
    auto& s = *m_state;
    int result = 0;
    while ((result = avcodec_receive_packet(s.codec, s.packet)) >= 0) {
        if (s.packet->duration <= 0) {
            s.packet->duration = av_rescale_q(1, AVRational{1, s.frameRate}, s.codec->time_base);
        }
        av_packet_rescale_ts(s.packet, s.codec->time_base, s.stream->time_base);
        s.packet->stream_index = s.stream->index;
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
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    s.scaler = sws_getCachedContext(s.scaler, s.size.width(), s.size.height(), AV_PIX_FMT_RGBA,
                                   s.size.width(), s.size.height(), AV_PIX_FMT_YUV420P,
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
        pts = qMax(s.lastPts + 1, timestampMs - s.originMs);
    }
    s.frame->pts = pts;
    result = avcodec_send_frame(s.codec, s.frame);
    if (result < 0) {
        fail(QStringLiteral("Cannot submit video frame: %1").arg(ffmpegError(result)));
        return;
    }
    s.lastPts = pts;
    if (drainPackets()) {
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
