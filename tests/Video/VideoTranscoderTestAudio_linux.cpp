#include "VideoTranscoderTestAudio.h"
#include "video/FFmpegMedia.h"
#include <QtEndian>

bool decodeAudioTrack(const QString& path, int sampleRate, DecodedAudio* out, QString* error)
{
    if (sampleRate != 48000) { if (error) *error = "Test decoder expects 48 kHz"; return false; }
    const auto probe = SnapTray::FFmpeg::probeFile(path);
    SnapTray::FFmpeg::AudioReader reader;
    if (!probe.hasAudio || !reader.load(path)) { if (error) *error = reader.error(); return false; }
    out->sampleRate = 48000;
    out->firstFrame = qMax<qint64>(0, probe.audioStartMs)*48;
    out->mono.clear();
    const qint64 end = probe.audioEndMs*48;
    for (qint64 start = out->firstFrame; start < end;) {
        const int count = int(qMin<qint64>(4800, end-start));
        const auto pcm = reader.read(start, count);
        if (pcm.size() != count*4) { if (error) *error = reader.error(); return false; }
        for (int i = 0; i < count; ++i) out->mono.push_back(qFromLittleEndian<qint16>(pcm.constData()+i*4)/32768.0f);
        start += count;
    }
    return true;
}
