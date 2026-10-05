#include "capture/PulseAudioCaptureEngine.h"
#include "capture/TimestampedPcmMixer.h"
#include <pulse/pulseaudio.h>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
#include <future>
#include <chrono>

namespace {
using namespace SnapTray::Audio;
// All PulseAudio objects stay on their owning thread. The bounded mainloop poll
// also makes shutdown independent of whether a device is delivering samples.
struct Connection {
    pa_mainloop* loop = pa_mainloop_new();
    pa_context* context = pa_context_new(pa_mainloop_get_api(loop), "SnapTray");
    ~Connection() { pa_context_disconnect(context); pa_context_unref(context); pa_mainloop_free(loop); }
    bool tick() {
        if (pa_mainloop_prepare(loop, 20) < 0 || pa_mainloop_poll(loop) < 0
            || pa_mainloop_dispatch(loop) < 0) return false;
        return PA_CONTEXT_IS_GOOD(pa_context_get_state(context));
    }
    bool connect() {
        if (pa_context_connect(context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) return false;
        QElapsedTimer timeout; timeout.start();
        while (pa_context_get_state(context) != PA_CONTEXT_READY) {
            if (timeout.elapsed() > 2000 || !tick()) return false;
        }
        return true;
    }
    bool wait(pa_operation* operation) {
        if (!operation) return false;
        QElapsedTimer timeout; timeout.start();
        while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING) {
            if (timeout.elapsed() > 2000 || !tick()) { pa_operation_cancel(operation); pa_operation_unref(operation); return false; }
        }
        const bool success = pa_operation_get_state(operation) == PA_OPERATION_DONE;
        pa_operation_unref(operation); return success;
    }
};
struct Devices {
    QList<IAudioCaptureEngine::AudioDevice> inputs;
    QString defaultInput;
    QString monitor;
};
Devices enumerate(Connection& connection) {
    Devices devices;
    QString sink;
    struct Defaults { Devices* devices; QString* sink; } defaults{&devices, &sink};
    connection.wait(pa_context_get_server_info(connection.context, [](pa_context*, const pa_server_info* info, void* data) {
        if (!info) return;
        auto& d = *static_cast<Defaults*>(data);
        d.devices->defaultInput = QString::fromUtf8(info->default_source_name);
        *d.sink = QString::fromUtf8(info->default_sink_name);
    }, &defaults));
    connection.wait(pa_context_get_source_info_list(connection.context, [](pa_context*, const pa_source_info* info, int end, void* data) {
        if (end || !info) return;
        auto& d = *static_cast<Devices*>(data);
        if (info->monitor_of_sink == PA_INVALID_INDEX) {
            const QString id = QString::fromUtf8(info->name);
            d.inputs.append({id, QString::fromUtf8(info->description), id == d.defaultInput});
        }
    }, &devices));
    if (!sink.isEmpty()) connection.wait(pa_context_get_sink_info_by_name(connection.context, sink.toUtf8().constData(),
        [](pa_context*, const pa_sink_info* info, int end, void* data) {
            if (!end && info) static_cast<Devices*>(data)->monitor = QString::fromUtf8(info->monitor_source_name);
        }, &devices));
    return devices;
}
}

struct PulseAudioCaptureEngine::State {
    std::atomic<bool> running{false}, paused{false};
    std::thread worker;
};
PulseAudioCaptureEngine::PulseAudioCaptureEngine(QObject* parent) : IAudioCaptureEngine(parent) {}
PulseAudioCaptureEngine::~PulseAudioCaptureEngine() { stop(); }
bool PulseAudioCaptureEngine::setAudioSource(AudioSource source) {
    if (isRunning() || source == AudioSource::None) return false;
    m_source = source; return true;
}
bool PulseAudioCaptureEngine::setDevice(const QString& id) {
    if (isRunning()) return false;
    m_deviceId = id; return true;
}
QList<IAudioCaptureEngine::AudioDevice> PulseAudioCaptureEngine::availableInputDevices() const {
    Connection c; return c.connect() ? enumerate(c).inputs : QList<AudioDevice>{};
}
QString PulseAudioCaptureEngine::defaultInputDevice() const {
    Connection c; return c.connect() ? enumerate(c).defaultInput : QString{};
}
bool PulseAudioCaptureEngine::isAvailable() const { Connection c; return c.connect(); }
bool PulseAudioCaptureEngine::isRunning() const { return d && d->running; }
bool PulseAudioCaptureEngine::isPaused() const { return d && d->paused; }
void PulseAudioCaptureEngine::pause() { if (d) d->paused = true; }
void PulseAudioCaptureEngine::resume() { if (d) d->paused = false; }
void PulseAudioCaptureEngine::stop() {
    if (!d) return;
    d->running = false;
    if (d->worker.joinable()) d->worker.join();
    d.reset();
}
bool PulseAudioCaptureEngine::start() {
    stop();
    if (m_source == AudioSource::None) return false;
    d = std::make_unique<State>(); d->running = true;
    std::promise<bool> ready; auto result = ready.get_future();
    // RecordingManager starts its clock immediately before start(). Include
    // audio-server setup in this same timeline rather than shifting audio early.
    QElapsedTimer captureClock; captureClock.start();
    d->worker = std::thread([this, ready = std::move(ready), clock = captureClock]() mutable {
        Connection connection;
        if (!connection.connect()) { d->running = false; ready.set_value(false); return; }
        const auto devices = enumerate(connection);
        TimestampedPcmMixer::Config config;
        config.microphoneEnabled = m_source == AudioSource::Microphone || m_source == AudioSource::Both;
        config.systemAudioEnabled = m_source == AudioSource::SystemAudio || m_source == AudioSource::Both;
        TimestampedPcmMixer mixer(config);
        qint64 pauseNs = 0, pauseStartNs = 0;
        bool paused = false;
        auto activeNs = [&] { return (paused ? pauseStartNs : clock.nsecsElapsed()) - pauseNs; };
        auto deliver = [this](const TimestampedPcmMixer::ProcessResult& result) {
            for (const auto& chunk : result.output) emit audioDataReady(chunk.pcm, chunk.startFrame);
        };
        struct Input {
            pa_stream* stream = nullptr;
            Source source;
            std::function<void(pa_stream*, Source)> read;
        } inputs[2];
        const pa_sample_spec spec{PA_SAMPLE_S16LE, 48000, 2};
        const pa_buffer_attr buffers{uint32_t(-1), uint32_t(-1), uint32_t(-1), uint32_t(-1), 1920};
        const QString names[] = {m_deviceId.isEmpty() ? devices.defaultInput : m_deviceId, devices.monitor};
        const bool enabled[] = {config.microphoneEnabled, config.systemAudioEnabled};
        bool initialized = true;
        for (int i = 0; i < 2; ++i) {
            if (!enabled[i]) continue;
            auto& input = inputs[i];
            input.source = i == 0 ? Source::Microphone : Source::SystemAudio;
            if (names[i].isEmpty()) { initialized = false; break; }
            input.stream = pa_stream_new(connection.context, i == 0 ? "Microphone" : "System audio", &spec, nullptr);
            if (!input.stream) { initialized = false; break; }
            input.read = [&](pa_stream* stream, Source source) {
                const void* data = nullptr; size_t bytes = 0;
                if (pa_stream_peek(stream, &data, &bytes) < 0) return;
                if (bytes && !paused) {
                    pa_usec_t latency = 0; int negative = 0;
                    if (pa_stream_get_latency(stream, &latency, &negative) < 0) {
                        latency = pa_bytes_to_usec(bytes, &spec); negative = 0;
                    }
                    const qint64 now = activeNs();
                    const qint64 start = qMax<qint64>(0, now - (negative ? -qint64(latency)*1000 : qint64(latency)*1000));
                    QByteArray pcm = data ? QByteArray(static_cast<const char*>(data), bytes) : QByteArray(bytes, '\0');
                    deliver(mixer.push(source, {pcm, start, {48000, 2}}, now));
                }
                if (bytes) pa_stream_drop(stream);
            };
            pa_stream_set_read_callback(input.stream, [](pa_stream* stream, size_t, void* data) {
                auto& input = *static_cast<Input*>(data); input.read(stream, input.source);
            }, &input);
            if (pa_stream_connect_record(input.stream, names[i].toUtf8().constData(), &buffers,
                    pa_stream_flags_t(PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE)) < 0) { initialized = false; break; }
        }
        QElapsedTimer timeout; timeout.start();
        while (initialized && d->running) {
            bool allReady = true;
            for (const auto& input : inputs) if (input.stream) {
                const auto state = pa_stream_get_state(input.stream);
                if (!PA_STREAM_IS_GOOD(state)) initialized = false;
                allReady = allReady && state == PA_STREAM_READY;
            }
            if (allReady || !initialized) break;
            if (timeout.elapsed() > 2000 || !connection.tick()) initialized = false;
        }
        ready.set_value(initialized && d->running);
        while (initialized && d->running) {
            const bool desiredPause = d->paused;
            if (desiredPause != paused) {
                if (desiredPause) { pauseStartNs = clock.nsecsElapsed(); deliver(mixer.pause(activeNs())); paused = true; }
                else { pauseNs += clock.nsecsElapsed() - pauseStartNs; paused = false; deliver(mixer.resume(activeNs())); }
                for (auto& input : inputs) if (input.stream) {
                    if (auto* op = pa_stream_flush(input.stream, nullptr, nullptr)) pa_operation_unref(op);
                    if (auto* op = pa_stream_cork(input.stream, paused, nullptr, nullptr)) pa_operation_unref(op);
                }
            }
            if (!connection.tick()) { initialized = false; break; }
            for (auto& input : inputs) if (input.stream && !PA_STREAM_IS_GOOD(pa_stream_get_state(input.stream))) initialized = false;
            if (!paused) deliver(mixer.advanceTo(activeNs()));
        }
        deliver(mixer.flush(activeNs()));
        for (auto& input : inputs) if (input.stream) {
            pa_stream_set_read_callback(input.stream, nullptr, nullptr);
            pa_stream_disconnect(input.stream); pa_stream_unref(input.stream);
        }
        if (!initialized && d->running) { emit warning(QStringLiteral("The audio server or capture device disconnected.")); emit activeSourceChanged(AudioSource::None); }
        d->running = false;
    });
    const bool success = result.get();
    if (!success) stop();
    return success;
}
