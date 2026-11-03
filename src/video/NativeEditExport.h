#pragma once

#include "video/IVideoTranscoder.h"
#include <QImage>

namespace SnapTray {

// Offline native MP4 writer and source PCM decoder. The shared export loop
// owns all timeline/effect decisions; these adapters only speak media APIs.
struct NativeEditFormat {
    QSize size;
    double fps = 30.0;
    int bitrate = 0;
    bool hevc = false;
    bool audio = false;
    qint64 durationMs = 0;
};

class NativeEditMedia {
public:
    virtual ~NativeEditMedia() = default;
    virtual bool open(const QString& input, const QString& output,
                      const NativeEditFormat& format, bool decodeAudio) = 0;
    virtual QByteArray audioAt(qint64 frame, int count) = 0; // 48 kHz stereo PCM16
    virtual bool writeVideo(const QImage& frame, qint64 timeUs, qint64 durationUs) = 0;
    virtual bool writeAudio(const QByteArray& pcm, qint64 startFrame) = 0;
    virtual bool finish() = 0;
    virtual void abort() = 0;
    virtual QString error() const = 0;
};

bool needsNativeEditExport(const VideoTranscodeRequest& request);
VideoTranscodeResult exportNativeEdit(const VideoTranscodeRequest& request,
    const IVideoTranscoder::ProgressCallback& progress, IVideoTranscoder& transcoder,
    NativeEditMedia& media);

// Decoders produce timestamped PCM packets; this supplies silence in timeline
// gaps and trims packet edges precisely without buffering the entire recording.
class NativePcmReader {
public:
    virtual ~NativePcmReader() = default;
    QByteArray read(qint64 frame, int count);
protected:
    // false at EOF or failure; failures set m_error. Packets own their bytes.
    virtual bool nextPacket(QByteArray& pcm, qint64& startFrame) = 0;
    QString m_error;
private:
    QByteArray m_packet;
    qint64 m_start = 0;
    bool m_eof = false;
};

std::unique_ptr<NativeEditMedia> createNativeEditMedia();
bool nativeHevcExportAvailable();
} // namespace SnapTray
