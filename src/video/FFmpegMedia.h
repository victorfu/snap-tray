#pragma once
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"
#include <QByteArray>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace SnapTray::FFmpeg {
QString errorString(int code);
// Owns a single decoder; never shared between decoding threads.
class Decoder {
public:
    ~Decoder();
    bool open(const QString& path, AVMediaType type);
    bool seek(qint64 positionMs);
    bool next();
    qint64 timeMs() const;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    QString error;
private:
    AVPacket* packet = nullptr;
    bool eof = false;
    void close();
};
class FrameReader final : public IVideoFrameReader {
public:
    explicit FrameReader(bool allowSeeking = false) : allowSeeking(allowSeeking) {}
    ~FrameReader() override;
    bool load(const QString& path) override;
    QImage frameAt(qint64 ms) override;
    QSize videoSize() const override;
    qint64 duration() const override;
    double frameRate() const override;
    QString lastError() const override { return decoder.error; }
    qint64 nextTimestampMs() const { return pending ? decoder.timeMs() : -1; }
private:
    Decoder decoder;
    SwsContext* scaler = nullptr;
    QImage current;
    bool pending = false;
    bool exhausted = false;
    qint64 lastRequest = -1;
    bool allowSeeking = false;
};
VideoFileProbe probeFile(const QString& path);
// Playback decoder returns bounded, interleaved 48 kHz stereo PCM. Missing
// source coverage is silent, keeping playback aligned to the video timeline.
class AudioReader {
public:
    ~AudioReader();
    bool load(const QString& path);
    QByteArray read(qint64 startFrame, int frames);
    QString error() const { return decoder.error; }
private:
    Decoder decoder;
    SwrContext* resampler = nullptr;
    QByteArray pending;
    qint64 pendingStart = 0;
    qint64 nextFrame = -1;
    bool exhausted = false;
};
}
