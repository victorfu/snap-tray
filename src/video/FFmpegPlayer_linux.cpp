#include "FFmpegMedia.h"
#include "video/IVideoPlayer.h"
#include <pulse/pulseaudio.h>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QTimer>
#include <QThread>
#include <QtEndian>

namespace {
// Nonblocking writes to a bounded PulseAudio buffer. The threaded mainloop can
// always be stopped, including when the server or output device disappears.
class AudioOutput {
public:
    ~AudioOutput() { close(); }
    bool open() {
        close();
        loop = pa_threaded_mainloop_new();
        context = pa_context_new(pa_threaded_mainloop_get_api(loop), "SnapTray preview");
        if (!context || pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0
            || pa_threaded_mainloop_start(loop) < 0) { close(); return false; }
        started = true;
        QElapsedTimer deadline; deadline.start();
        while (deadline.elapsed() < 2000) {
            pa_threaded_mainloop_lock(loop);
            const auto state = pa_context_get_state(context);
            pa_threaded_mainloop_unlock(loop);
            if (state == PA_CONTEXT_READY) break;
            if (!PA_CONTEXT_IS_GOOD(state)) { close(); return false; }
            QThread::msleep(5);
        }
        pa_threaded_mainloop_lock(loop);
        const pa_sample_spec spec{PA_SAMPLE_S16LE, 48000, 2};
        stream = pa_stream_new(context, "Recording playback", &spec, nullptr);
        // 150 ms maximum buffering, 20 ms minimum requests, start corked.
        const pa_buffer_attr buffer{28800, 19200, 0, 3840, uint32_t(-1)};
        const int status = stream ? pa_stream_connect_playback(stream, nullptr, &buffer,
            pa_stream_flags_t(PA_STREAM_START_CORKED | PA_STREAM_VARIABLE_RATE | PA_STREAM_ADJUST_LATENCY
                | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING), nullptr, nullptr) : -1;
        pa_threaded_mainloop_unlock(loop);
        if (status < 0) { close(); return false; }
        deadline.restart();
        while (deadline.elapsed() < 2000) {
            pa_threaded_mainloop_lock(loop); const auto state = pa_stream_get_state(stream); pa_threaded_mainloop_unlock(loop);
            if (state == PA_STREAM_READY) return true;
            if (!PA_STREAM_IS_GOOD(state)) break;
            QThread::msleep(5);
        }
        close(); return false;
    }
    void close() {
        if (started) pa_threaded_mainloop_stop(loop);
        if (stream) { pa_stream_disconnect(stream); pa_stream_unref(stream); }
        if (context) { pa_context_disconnect(context); pa_context_unref(context); }
        if (loop) pa_threaded_mainloop_free(loop);
        loop = nullptr; context = nullptr; stream = nullptr; started = false;
    }
    void reset(bool playing, float rate) {
        if (!stream) return;
        pa_threaded_mainloop_lock(loop);
        if (auto* op = pa_stream_cork(stream, !playing, nullptr, nullptr)) pa_operation_unref(op);
        if (auto* op = pa_stream_flush(stream, nullptr, nullptr)) pa_operation_unref(op);
        if (auto* op = pa_stream_update_sample_rate(stream, qRound(48000*rate), nullptr, nullptr)) pa_operation_unref(op);
        pa_threaded_mainloop_unlock(loop);
    }
    int writableFrames() const {
        if (!stream) return 0;
        pa_threaded_mainloop_lock(loop);
        const size_t bytes = pa_stream_writable_size(stream);
        pa_threaded_mainloop_unlock(loop);
        return bytes == size_t(-1) ? 0 : int(qMin<size_t>(bytes/4, 9600));
    }
    qint64 latencyUs() const {
        if (!stream) return -1;
        pa_usec_t value = 0; int negative = 0;
        pa_threaded_mainloop_lock(loop);
        const int result = pa_stream_get_latency(stream, &value, &negative);
        pa_threaded_mainloop_unlock(loop);
        return result < 0 ? -1 : (negative ? -qint64(value) : qint64(value));
    }
    bool healthy() const {
        if (!stream) return false;
        pa_threaded_mainloop_lock(loop);
        const bool result = PA_STREAM_IS_GOOD(pa_stream_get_state(stream));
        pa_threaded_mainloop_unlock(loop); return result;
    }
    bool write(QByteArray data, float volume) {
        if (!stream) return false;
        if (volume < 1.0f) for (qsizetype i = 0; i < data.size(); i += 2) {
            const qint16 sample = qRound(qFromLittleEndian<qint16>(data.constData()+i)*volume);
            qToLittleEndian(sample, data.data()+i);
        }
        pa_threaded_mainloop_lock(loop);
        const size_t available = pa_stream_writable_size(stream);
        const bool success = available != size_t(-1) && size_t(data.size()) <= available
            && pa_stream_write(stream, data.constData(), data.size(), nullptr, 0, PA_SEEK_RELATIVE) == 0;
        pa_threaded_mainloop_unlock(loop); return success;
    }
private:
    pa_threaded_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    pa_stream* stream = nullptr;
    bool started = false;
};

class FFmpegPlayer final : public IVideoPlayer {
public:
    explicit FFmpegPlayer(QObject* parent) : IVideoPlayer(parent) {
        timer.setInterval(10);
        timer.setTimerType(Qt::PreciseTimer);
        connect(&timer, &QTimer::timeout, this, [this] { tick(); });
        connect(&watcher, &QFutureWatcher<Decoded>::finished, this, [this] {
            const auto decoded = watcher.result();
            if (decoded.generation != generation) { dispatch(); return; }
            if (!decoded.error.isEmpty()) { pause(); emit error(decoded.error); return; }
            if (!decoded.image.isNull()) emit frameReady(decoded.image);
            if (!decoded.pcm.isEmpty() && m_state == State::Playing) {
                if (output.write(decoded.pcm, muted ? 0.0f : gain)) audioCursor = decoded.audioEnd;
                else {
                    // Retry at the current clock after an underrun or full buffer;
                    // never let delayed output grow without bound.
                    audioCursor = qMax(audioCursor, position()*48);
                }
            }
            if (requestedPosition != decoded.position) dispatch();
        });
    }
    ~FFmpegPlayer() override { timer.stop(); watcher.waitForFinished(); }
    bool load(const QString& path) override {
        stop(); watcher.waitForFinished(); ++generation; output.close();
        media = SnapTray::FFmpeg::probeFile(path);
        video = std::make_unique<SnapTray::FFmpeg::FrameReader>(true);
        audio = std::make_unique<SnapTray::FFmpeg::AudioReader>();
        if (!media.valid || !video->load(path)) { emit error(video->lastError()); media = {}; return false; }
        audioReady = media.hasAudio && audio->load(path) && output.open();
        if (media.hasAudio && !audioReady) emit error(QStringLiteral("Audio playback is unavailable. Check the audio server and output device."));
        requestedPosition = 0; positionBase = 0; audioCursor = 0;
        // Deliver the first frame before mediaLoaded: consumers may immediately
        // inspect the frame size or request an export after successful load().
        const auto first = video->frameAt(0);
        if (first.isNull()) { emit error(video->lastError()); media = {}; return false; }
        emit frameReady(first); emit durationChanged(duration()); emit mediaLoaded(); return true;
    }
    void play() override {
        if (!hasVideo() || m_state == State::Playing) return;
        if (positionBase >= duration()) seek(0);
        m_state = State::Playing; clock.restart();
        audioCursor = positionBase*48; output.reset(true, rate); ++generation;
        timer.start(); emit stateChanged(m_state); tick();
    }
    void pause() override {
        if (m_state != State::Playing) return;
        positionBase = position(); m_state = State::Paused; timer.stop();
        output.reset(false, rate); ++generation; emit stateChanged(m_state);
    }
    void stop() override {
        timer.stop(); ++generation; m_state = State::Stopped; positionBase = 0;
        requestedPosition = 0; audioCursor = 0; output.reset(false, rate);
        emit stateChanged(m_state); emit positionChanged(0);
    }
    void seek(qint64 ms) override {
        positionBase = qBound<qint64>(0, ms, duration()); requestedPosition = positionBase;
        clock.restart(); audioCursor = positionBase*48; ++generation;
        output.reset(m_state == State::Playing, rate); emit positionChanged(positionBase); dispatch();
    }
    State state() const override { return m_state; }
    qint64 duration() const override { return media.durationMs; }
    qint64 position() const override {
        if (m_state != State::Playing) return positionBase;
        if (audioReady) {
            const qint64 latency = output.latencyUs();
            if (latency >= 0) return qBound(positionBase,
                audioCursor/48-qint64(latency*rate/1000), duration());
        }
        return qMin(duration(), positionBase + qint64(clock.elapsed()*rate));
    }
    QSize videoSize() const override { return media.videoSize; }
    bool hasVideo() const override { return media.valid; }
    bool hasAudio() const override { return media.hasAudio; }
    bool supportsAudioPlayback() const override { return audioReady; }
    double frameRate() const override { return media.frameRate; }
    int frameIntervalMs() const override { return qMax(1, qRound(1000/qMax(1.0, frameRate()))); }
    void setVolume(float value) override { gain = qBound(0.0f, value, 1.0f); }
    float volume() const override { return gain; }
    void setMuted(bool value) override { muted = value; }
    bool isMuted() const override { return muted; }
    void setLooping(bool value) override { loop = value; }
    bool isLooping() const override { return loop; }
    void setPlaybackRate(float value) override {
        const qint64 current = position(); rate = qBound(0.25f, value, 2.0f);
        seek(current); emit playbackRateChanged(rate);
    }
    float playbackRate() const override { return rate; }
private:
    struct Decoded { QImage image; QByteArray pcm; QString error; quint64 generation; qint64 position; qint64 audioEnd; };
    void tick() {
        if (audioReady && !output.healthy()) {
            pause(); emit error(QStringLiteral("The playback audio device disconnected.")); return;
        }
        requestedPosition = position(); emit positionChanged(requestedPosition);
        if (requestedPosition >= duration()) {
            if (loop) seek(0);
            else { pause(); positionBase = duration(); emit playbackFinished(); return; }
        }
        dispatch();
    }
    void dispatch() {
        if (!hasVideo() || watcher.isRunning()) return;
        const auto target = requestedPosition;
        const auto token = generation;
        const bool withAudio = audioReady && m_state == State::Playing;
        const qint64 audioStart = qMax(audioCursor, target*48);
        const int audioCount = withAudio ? int(qBound<qint64>(qint64(0),
            (target+qRound(100*rate))*48-audioStart, qint64(output.writableFrames()))) : 0;
        watcher.setFuture(QtConcurrent::run([this, token, target, audioStart, audioCount] {
            Decoded result; result.generation = token; result.position = target; result.audioEnd = audioStart+audioCount;
            result.image = video->frameAt(target); result.error = video->lastError();
            if (audioCount) { result.pcm = audio->read(audioStart, audioCount); if (result.pcm.isEmpty()) result.error = audio->error(); }
            return result;
        }));
    }
    std::unique_ptr<SnapTray::FFmpeg::FrameReader> video;
    std::unique_ptr<SnapTray::FFmpeg::AudioReader> audio;
    VideoFileProbe media;
    AudioOutput output;
    QFutureWatcher<Decoded> watcher;
    QTimer timer;
    QElapsedTimer clock;
    State m_state = State::Stopped;
    qint64 positionBase = 0, requestedPosition = 0, audioCursor = 0;
    quint64 generation = 0;
    bool audioReady = false, muted = false, loop = false;
    float gain = 1.0f, rate = 1.0f;
};
}
IVideoPlayer* createFFmpegPlayer(QObject* parent) { return new FFmpegPlayer(parent); }
