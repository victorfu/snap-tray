#pragma once

// Run the shared native edit loop on Linux using the installed FFmpeg codec.
// Native adapters are exercised by the same suite on macOS/Windows CI.
#include "video/NativeEditExport.h"
#include "video/FFmpegMedia.h"
#include "encoding/FFmpegEncoder.h"
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>

class NativeEditTestMedia final : public SnapTray::NativeEditMedia {
public:
    bool open(const QString& input, const QString& output, const SnapTray::NativeEditFormat& f, bool decodeAudio) override {
        hasAudio = f.audio;
        if (decodeAudio && !audio.load(input)) return false;
        encoder.setBitrate(f.bitrate);
        encoder.setHevc(f.hevc);
        if (f.audio) encoder.setAudioFormat(48000, 2, 16);
        return encoder.start(output, f.size, qRound(f.fps)) && (!f.audio || encoder.isAudioEnabled());
    }
    bool concurrentAudio() const override { return qEnvironmentVariableIsSet("SNAPTRAY_TEST_CONCURRENT_AUDIO"); }
    void cancel() override { QMutexLocker lock(&mutex); cancelled = true; audioReady.wakeAll(); }
    QByteArray audioAt(qint64 start, int count) override { QMutexLocker lock(&mutex); return audio.read(start, count); }
    bool writeVideo(const QImage& image, qint64 timeUs, qint64) override {
        QMutexLocker lock(&mutex);
        // Model a muxer that requires the other track to advance before it
        // accepts video. Serial video-then-audio feeding cannot make progress.
        if (concurrentAudio() && hasAudio && !audioWritten && !cancelled)
            audioReady.wait(&mutex, 2000);
        if (cancelled || (concurrentAudio() && hasAudio && !audioWritten)) {
            harnessError = QStringLiteral("Video input waited for audio progress.");
            return false;
        }
        encoder.writeFrame(image, timeUs / 1000);
        return encoder.isRunning();
    }
    bool writeAudio(const QByteArray& pcm, qint64 start) override {
        QMutexLocker lock(&mutex);
        if (cancelled) return false;
        encoder.writeAudioSamples(pcm, start);
        audioWritten = true;
        audioReady.wakeAll();
        return encoder.isRunning();
    }
    bool finish() override { encoder.finish(); return encoder.lastError().isEmpty(); }
    void abort() override { if (encoder.isRunning()) encoder.abort(); }
    QString error() const override {
        QMutexLocker lock(&mutex);
        if (!harnessError.isEmpty()) return harnessError;
        return encoder.lastError().isEmpty() ? audio.error() : encoder.lastError();
    }
private:
    mutable QMutex mutex;
    QWaitCondition audioReady;
    bool hasAudio = false, audioWritten = false, cancelled = false;
    QString harnessError;
    FFmpegEncoder encoder;
    SnapTray::FFmpeg::AudioReader audio;
};

class NativeEditTestTranscoder final : public IVideoTranscoder, public VideoTranscoderFaultInjection {
public:
    VideoFileProbe probe(const QString& path) override {
        auto result = delegate->probe(path);
        if (path == sourceRatePath) result.frameRate = sourceRate;
        return result;
    }
    void setSourceFrameRateForTesting(const QString& path, double rate) {
        sourceRatePath = path;
        sourceRate = rate;
    }
    bool supportsFrameFilter() const override { return true; }
    bool supportsOutputOptions() const override { return true; }
    bool supportsSegments() const override { return true; }
    bool supportsAudioMix() const override { return true; }
    bool supportsVideoCodec(const QString& codec) const override { return delegate->supportsVideoCodec(codec); }
    void setFaultForTesting(VideoTranscodeFault fault) override {
        if (auto* injection = dynamic_cast<VideoTranscoderFaultInjection*>(delegate.get())) injection->setFaultForTesting(fault);
    }
    VideoTranscodeResult transcode(const VideoTranscodeRequest& r, const ProgressCallback& progress) override {
        if (!SnapTray::needsNativeEditExport(r)) return delegate->transcode(r, progress);
        NativeEditTestMedia media;
        return SnapTray::exportNativeEdit(r, progress, *this, media);
    }
private:
    QString sourceRatePath;
    double sourceRate = 0;
    std::unique_ptr<IVideoTranscoder> delegate = IVideoTranscoder::create();
};
