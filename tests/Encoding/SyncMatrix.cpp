#include "SyncMatrix.h"

#include "IVideoEncoder.h"
#include "capture/TimestampedPcmMixer.h"
#include "recording/FramePacer.h"
#include "recording/SessionClock.h"
#include "video/FFmpegMedia.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMultiHash>
#include <QPainter>
#include <QRandomGenerator>
#include <QSet>
#include <QSignalSpy>
#include <QThread>
#include <QtEndian>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <memory>

namespace SyncMatrix {

namespace {

using SnapTray::Audio::Source;
using SnapTray::Audio::TimestampedPcmMixer;

constexpr qint64 kNsPerMs = 1000000;
constexpr int kIdBits = 16;              // frame ID barcode width
constexpr qint64 kFlashFirstMs = 350;    // flash + audio pulse schedule (wall clock)
constexpr qint64 kFlashEveryMs = 700;
constexpr qint64 kPulseMs = 40;
constexpr double kPulseHz = 1000.0;
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr qint64 kAudioChunkMs = 10;      // capture callback period
constexpr qint64 kAudioLatencyMs = 5;     // callback arrives after its last sample
constexpr qint64 kTruncateTailMs = 500;   // TruncateTail drops this much at the end
constexpr qint64 kShiftAudioMs = 120;     // ShiftAudio moves audio this much
constexpr qint64 kWriteRetryMs = 2000;    // an encoder applying backpressure
constexpr int kPulseWindowFrames = 240;   // 5 ms analysis windows
constexpr double kPulseThreshold = 0.1;   // RMS (full scale 1.0) that counts as sound

// --- Synthetic content -----------------------------------------------------

QImage makeFrame(const QSize& size, int id, bool flash, int motion)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(64, 64, 64));
    QPainter painter(&image);
    const int band = size.height() / 8;
    const int cell = size.width() / kIdBits;
    for (int bit = 0; bit < kIdBits; ++bit) {
        painter.fillRect(bit * cell, 0, cell, band, (id >> bit) & 1 ? Qt::white : Qt::black);
    }
    if (flash) {
        painter.fillRect(0, size.height() / 4, size.width(), size.height() - size.height() / 4, Qt::white);
    } else {
        // Something moving, kept darker than a flash.
        const int side = std::max(8, size.height() / 9);
        painter.fillRect((motion * 8) % std::max(1, size.width() - side), size.height() / 2, side, side,
                         QColor(110, 110, 110));
    }
    return image;
}

int meanGray(const QImage& image, const QRect& rect)
{
    qint64 sum = 0;
    int count = 0;
    const QRect r = rect & image.rect();
    for (int y = r.top(); y <= r.bottom(); y += 2) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = r.left(); x <= r.right(); x += 2) {
            sum += qGray(line[x]);
            ++count;
        }
    }
    return count ? int(sum / count) : 0;
}

int readId(const QImage& frame)
{
    const int band = frame.height() / 8;
    const int cell = frame.width() / kIdBits;
    int id = 0;
    for (int bit = 0; bit < kIdBits; ++bit) {
        // The middle half of each cell, away from codec ringing at its edges.
        const QRect probe(bit * cell + cell / 4, band / 4, std::max(1, cell / 2), std::max(1, band / 2));
        if (meanGray(frame, probe) > 128) id |= 1 << bit;
    }
    return id;
}

bool isFlash(const QImage& frame)
{
    return meanGray(frame, QRect(0, frame.height() / 4, frame.width(), frame.height() * 3 / 4)) > 200;
}

bool pulseAt(qint64 wallMs, double* phaseOut, qint64 sampleIndex)
{
    for (qint64 f = kFlashFirstMs; f <= wallMs; f += kFlashEveryMs) {
        if (wallMs >= f && wallMs < f + kPulseMs) {
            *phaseOut = 2.0 * M_PI * kPulseHz * double(sampleIndex) / kSampleRate;
            return true;
        }
    }
    return false;
}

QByteArray audioChunk(qint64 wallStartMs, qint64 lengthMs, double amplitude)
{
    const int frames = int(lengthMs * kSampleRate / 1000);
    QByteArray pcm(frames * kChannels * 2, '\0');
    auto* out = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < frames; ++i) {
        const qint64 sample = wallStartMs * kSampleRate / 1000 + i;
        const qint64 wallMs = sample * 1000 / kSampleRate;
        double phase = 0;
        const qint16 v = pulseAt(wallMs, &phase, sample) ? qint16(amplitude * 32767 * std::sin(phase)) : 0;
        out[i * 2] = v;
        out[i * 2 + 1] = v;
    }
    return pcm;
}

// --- Events ------------------------------------------------------------------

struct VideoEvent {
    double arrivalMs;
    double sourceMs;
    int id;
    bool flash;
    int motion;
};

std::vector<VideoEvent> videoEvents(const Scenario& s)
{
    QRandomGenerator rng(0x5eed + quint32(s.fps) * 7919 + quint32(s.durationMs));
    const double interval = 1000.0 / s.fps;
    const int count = int(s.durationMs * s.fps / 1000);
    std::vector<VideoEvent> events;
    int staticId = -1;
    // The frame shown at a flash time is the first frame at or after it.
    std::vector<int> flashFrames;
    for (qint64 f = kFlashFirstMs; f < s.durationMs; f += kFlashEveryMs) flashFrames.push_back(int(std::ceil(f / interval)));
    for (int i = 0; i < count; ++i) {
        const double ideal = i * interval;
        const bool still = s.staticFromMs >= 0 && ideal >= s.staticFromMs && ideal < s.staticToMs;
        if (still && staticId < 0) staticId = i;
        if (!still) staticId = -1;
        const int id = still ? staticId : i;
        const bool flash = !still && std::find(flashFrames.begin(), flashFrames.end(), i) != flashFrames.end();
        if (s.dropEvery && i % s.dropEvery == s.dropEvery - 1) continue;
        const double jitter = s.sourceJitterMs ? (rng.generateDouble() * 2 - 1) * s.sourceJitterMs : 0;
        const double source = std::max(0.0, ideal + jitter);
        double arrival = source + s.arrivalDelayMs;
        if (s.burstEvery) arrival = (std::floor(i / double(s.burstEvery)) + 1) * s.burstEvery * interval + s.arrivalDelayMs;
        events.push_back({arrival, source, id, flash, i});
        if (s.duplicateEvery && i % s.duplicateEvery == s.duplicateEvery - 1) events.push_back({arrival + 0.1, source, id, flash, i});
        if (s.backwardEvery && i % s.backwardEvery == s.backwardEvery - 1 && i >= 2) {
            events.push_back({arrival + 0.2, source - 2 * interval, id, flash, i});
        }
    }
    std::stable_sort(events.begin(), events.end(),
                     [](const VideoEvent& a, const VideoEvent& b) { return a.arrivalMs < b.arrivalMs; });
    return events;
}

struct ExpectedFrame {
    qint64 ms;
    int id;
    bool flash;
};

struct Control {
    double wallMs;
    bool pause; // false = resume
};

std::vector<Control> controls(const Scenario& s)
{
    std::vector<Control> list;
    for (const auto& p : s.pauses) {
        list.push_back({double(p.atMs), true});
        list.push_back({double(p.atMs + p.lengthMs), false});
    }
    return list;
}

// Recording time of a wall time, pauses removed.
qint64 recordingMs(const Scenario& s, qint64 wallMs)
{
    qint64 removed = 0;
    for (const auto& p : s.pauses) {
        if (wallMs >= p.atMs + p.lengthMs) removed += p.lengthMs;
        else if (wallMs >= p.atMs) return p.atMs - removed; // inside a pause
    }
    return wallMs - removed;
}

bool insidePause(const Scenario& s, qint64 wallMs)
{
    for (const auto& p : s.pauses) {
        if (wallMs >= p.atMs && wallMs < p.atMs + p.lengthMs) return true;
    }
    return false;
}

// The reference: what a correct pipeline accepts and where it places it.
std::vector<ExpectedFrame> expectedFrames(const Scenario& s, const std::vector<VideoEvent>& events)
{
    SnapTray::SessionClock clock;
    SnapTray::FramePacer pacer(s.fps);
    clock.start(0);
    auto ctl = controls(s);
    size_t next = 0;
    std::vector<ExpectedFrame> frames;
    for (const auto& e : events) {
        while (next < ctl.size() && ctl[next].wallMs <= e.arrivalMs) {
            if (ctl[next].pause) { clock.pause(qint64(ctl[next].wallMs * kNsPerMs)); pacer.notePause(); }
            else clock.resume(qint64(ctl[next].wallMs * kNsPerMs));
            ++next;
        }
        qint64 recNs = 0;
        if (clock.admit(qint64(e.sourceMs * kNsPerMs), &recNs) != SnapTray::SessionClock::Admission::Accepted) continue;
        if (pacer.onFrame(recNs) != SnapTray::FramePacer::Decision::Emit) continue;
        frames.push_back({SnapTray::SessionClock::toEncoderMs(recNs), e.id, e.flash});
    }
    return frames;
}

// --- Audio pipeline (as the capture engines drive the mixer) -------------------

struct AudioPath {
    const Scenario& s;
    IVideoEncoder* encoder;
    TimestampedPcmMixer mixer;
    bool paused = false;
    double pausedAudioMs = 0;
    double pauseStartAudioMs = 0;
    qint64 writtenFrames = 0;
    qint64 cutoffFrame = -1; // TruncateTail: nothing written from here

    static TimestampedPcmMixer::Config config(const Scenario& s)
    {
        TimestampedPcmMixer::Config c;
        c.microphoneEnabled = s.audio == Audio::Microphone || s.audio == Audio::Mixed;
        c.systemAudioEnabled = s.audio == Audio::SystemAudio || s.audio == Audio::Mixed;
        return c;
    }
    AudioPath(const Scenario& scenario, IVideoEncoder* e) : s(scenario), encoder(e), mixer(config(scenario)) {}

    // The engine's own clock: started audioClockOffsetMs after the video epoch.
    double audioClockMs(double wallMs) const
    {
        return (wallMs - s.audioClockOffsetMs) * (1.0 + s.audioDriftPpm * 1e-6);
    }
    double activeMs(double wallMs) const
    {
        return (paused ? pauseStartAudioMs : audioClockMs(wallMs)) - pausedAudioMs;
    }
    void deliver(const TimestampedPcmMixer::ProcessResult& result)
    {
        for (const auto& chunk : result.output) {
            qint64 start = chunk.startFrame;
            if (s.mutation == Mutation::ShiftAudio) start += kShiftAudioMs * kSampleRate / 1000;
            if (cutoffFrame >= 0 && start >= cutoffFrame) continue;
            encoder->writeAudioSamples(chunk.pcm, start);
            writtenFrames = std::max(writtenFrames, start + chunk.pcm.size() / (kChannels * 2));
        }
    }
    void pause(double wallMs)
    {
        pauseStartAudioMs = audioClockMs(wallMs);
        deliver(mixer.pause(qint64(activeMs(wallMs) * kNsPerMs)));
        paused = true;
    }
    void resume(double wallMs)
    {
        pausedAudioMs += audioClockMs(wallMs) - pauseStartAudioMs;
        paused = false;
        deliver(mixer.resume(qint64(activeMs(wallMs) * kNsPerMs)));
    }
    // One capture callback for audio captured at wall [startMs, startMs + kAudioChunkMs).
    void callback(double startMs)
    {
        if (paused || startMs < s.audioClockOffsetMs) return; // the engine is corked / not started
        const double arrival = startMs + kAudioChunkMs + kAudioLatencyMs;
        const qint64 nowNs = qint64(activeMs(arrival) * kNsPerMs);
        const qint64 startNs = std::max<qint64>(0, nowNs - qint64((kAudioChunkMs + kAudioLatencyMs) * kNsPerMs));
        const bool gap = s.audioGapFromMs >= 0 && startMs >= s.audioGapFromMs && startMs < s.audioGapToMs;
        const SnapTray::Audio::Pcm16Format format{kSampleRate, kChannels};
        if (hasSource(Source::Microphone) && !gap) {
            deliver(mixer.push(Source::Microphone, {audioChunk(qint64(startMs), kAudioChunkMs, 0.5), startNs, format}, nowNs));
        }
        if (hasSource(Source::SystemAudio)) {
            deliver(mixer.push(Source::SystemAudio, {audioChunk(qint64(startMs), kAudioChunkMs, 0.3), startNs, format}, nowNs));
        }
        deliver(mixer.advanceTo(nowNs));
    }
    bool hasSource(Source source) const
    {
        const auto c = config(s);
        return source == Source::Microphone ? c.microphoneEnabled : c.systemAudioEnabled;
    }
    void flush(double wallMs) { deliver(mixer.flush(qint64(activeMs(wallMs) * kNsPerMs))); }
};

// --- Analysis -----------------------------------------------------------------

struct DecodedFrame {
    qint64 ms;
    int id;
    bool flash;
};

std::vector<DecodedFrame> decodeVideo(const QString& path, QString* error)
{
    SnapTray::FFmpeg::FrameReader reader;
    std::vector<DecodedFrame> frames;
    if (!reader.load(path)) {
        *error = reader.lastError();
        return frames;
    }
    qint64 t = 0;
    for (int guard = 0; guard < 1000000; ++guard) {
        const QImage frame = reader.frameAt(t);
        if (frame.isNull()) break;
        frames.push_back({t, readId(frame), isFlash(frame)});
        const qint64 next = reader.nextTimestampMs();
        if (next < 0) break;
        if (next <= t) {
            *error = QStringLiteral("non-increasing presentation time %1 after %2 ms").arg(next).arg(t);
            break;
        }
        t = next;
    }
    return frames;
}

// Pulse onsets (ms) and audio length (ms) of the decoded first audio track.
std::vector<qint64> decodePulses(const QString& path, qint64* lengthMs, QString* error)
{
    SnapTray::FFmpeg::AudioReader reader;
    std::vector<qint64> onsets;
    *lengthMs = 0;
    // The reader pads silence past the end; the container says where audio ends.
    const VideoFileProbe probe = SnapTray::FFmpeg::probeFile(path);
    if (!probe.hasAudio) {
        *error = QStringLiteral("no audio track");
        return onsets;
    }
    const qint64 endFrame = probe.audioEndMs * kSampleRate / 1000;
    if (!reader.load(path)) {
        *error = reader.error();
        return onsets;
    }
    bool sounding = false;
    qint64 frame = 0;
    constexpr int kChunk = 4800;
    while (frame < endFrame) {
        const QByteArray pcm = reader.read(frame, int(std::min<qint64>(kChunk, endFrame - frame)));
        if (pcm.isEmpty()) break;
        const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
        const int frames = int(pcm.size() / (kChannels * 2));
        for (int w = 0; w + kPulseWindowFrames <= frames; w += kPulseWindowFrames) {
            double energy = 0;
            for (int i = w; i < w + kPulseWindowFrames; ++i) {
                const double v = s[i * 2] / 32768.0;
                energy += v * v;
            }
            const bool loud = std::sqrt(energy / kPulseWindowFrames) > kPulseThreshold;
            if (loud && !sounding) onsets.push_back((frame + w) * 1000 / kSampleRate);
            sounding = loud;
        }
        frame += frames;
    }
    *lengthMs = probe.audioEndMs;
    return onsets;
}

qint64 residentKiB()
{
#ifdef Q_OS_LINUX
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (statm.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> parts = statm.readAll().split(' ');
        if (parts.size() > 1) return parts[1].toLongLong() * 4;
    }
#endif
    return -1;
}

const char* audioName(Audio a)
{
    switch (a) {
    case Audio::None: return "none";
    case Audio::Microphone: return "microphone";
    case Audio::SystemAudio: return "system";
    case Audio::Mixed: return "mixed";
    }
    return "?";
}

const char* mutationName(Mutation m)
{
    switch (m) {
    case Mutation::None: return "none";
    case Mutation::IgnorePause: return "ignore-pause";
    case Mutation::ShiftAudio: return "shift-audio";
    case Mutation::TruncateTail: return "truncate-tail";
    case Mutation::ReorderTimestamps: return "reorder-timestamps";
    }
    return "?";
}

} // namespace

QJsonObject Scenario::toJson() const
{
    QJsonArray pauseList;
    for (const auto& p : pauses) pauseList.append(QJsonObject{{"atMs", p.atMs}, {"lengthMs", p.lengthMs}});
    return {{"name", name}, {"width", size.width()}, {"height", size.height()}, {"fps", fps},
            {"durationMs", durationMs}, {"audio", audioName(audio)}, {"sourceJitterMs", sourceJitterMs},
            {"arrivalDelayMs", arrivalDelayMs}, {"burstEvery", burstEvery}, {"dropEvery", dropEvery},
            {"duplicateEvery", duplicateEvery}, {"backwardEvery", backwardEvery},
            {"staticFromMs", staticFromMs}, {"staticToMs", staticToMs}, {"pauses", pauseList},
            {"audioGapFromMs", audioGapFromMs}, {"audioGapToMs", audioGapToMs},
            {"audioClockOffsetMs", audioClockOffsetMs}, {"audioDriftPpm", audioDriftPpm},
            {"slowEncoderMs", slowEncoderMs}, {"mutation", mutationName(mutation)}};
}

QJsonObject Result::toJson(const Scenario& s) const
{
    return {{"scenario", s.toJson()}, {"passed", passed()}, {"failures", QJsonArray::fromStringList(failures)},
            {"skipped", QJsonArray::fromStringList(skipped)}, {"expected", expected}, {"measured", measured},
            {"cost", cost}};
}

void writeReport(const Scenario& scenario, const Result& result)
{
    const QString dir = qEnvironmentVariable("SNAPTRAY_SYNC_REPORT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(scenario.name + QStringLiteral(".json")));
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(result.toJson(scenario)).toJson());
}

Result run(const Scenario& s, const QString& workDir)
{
    Result result;
    result.scenario = s.name;
    const QString output = QDir(workDir).filePath(s.name + QStringLiteral(".mp4"));
    const double interval = 1000.0 / s.fps;
    const auto events = videoEvents(s);
    const auto expected = expectedFrames(s, events);

    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) {
        result.skipped << QStringLiteral("no native encoder on this platform");
        return result;
    }
    const bool withAudio = s.audio != Audio::None;
    if (withAudio) encoder->setAudioFormat(kSampleRate, kChannels, 16);
    QElapsedTimer wall;
    wall.start();
    const std::clock_t cpuStart = std::clock();
    if (!encoder->start(output, s.size, s.fps)) {
        result.failures << QStringLiteral("encoder did not start: %1").arg(encoder->lastError());
        return result;
    }
    if (withAudio && !encoder->isAudioEnabled()) result.failures << QStringLiteral("encoder refused the audio track");

    // The pipeline under test: the same pieces, in arrival order.
    SnapTray::SessionClock clock;
    SnapTray::FramePacer pacer(s.fps);
    clock.start(0);
    AudioPath audio(s, encoder.get());
    const qint64 lastFrameMs = expected.empty() ? 0 : expected.back().ms;
    const qint64 cutoffMs = s.mutation == Mutation::TruncateTail ? lastFrameMs - kTruncateTailMs : -1;
    if (cutoffMs >= 0) audio.cutoffFrame = cutoffMs * kSampleRate / 1000;
    const auto ctl = controls(s);
    size_t nextControl = 0;
    double nextAudio = 0;
    qint64 framesWritten = 0;
    qint64 encoderRejected = 0;
    qint64 lastStall = 0;
    const auto runControlsUntil = [&](double wallMs) {
        while (nextControl < ctl.size() && ctl[nextControl].wallMs <= wallMs) {
            const auto& c = ctl[nextControl++];
            if (c.pause) {
                if (s.mutation != Mutation::IgnorePause) clock.pause(qint64(c.wallMs * kNsPerMs));
                pacer.notePause();
                if (withAudio) audio.pause(c.wallMs);
            } else {
                if (s.mutation != Mutation::IgnorePause) clock.resume(qint64(c.wallMs * kNsPerMs));
                if (withAudio) audio.resume(c.wallMs);
            }
        }
    };
    const auto runAudioUntil = [&](double wallMs) {
        while (withAudio && nextAudio + kAudioChunkMs + kAudioLatencyMs <= wallMs && nextAudio < s.durationMs) {
            runControlsUntil(nextAudio + kAudioChunkMs + kAudioLatencyMs);
            if (!insidePause(s, qint64(nextAudio))) audio.callback(nextAudio);
            nextAudio += kAudioChunkMs;
        }
    };
    int written = 0;
    for (const auto& e : events) {
        runAudioUntil(e.arrivalMs);
        runControlsUntil(e.arrivalMs);
        qint64 recNs = 0;
        if (clock.admit(qint64(e.sourceMs * kNsPerMs), &recNs) != SnapTray::SessionClock::Admission::Accepted) continue;
        if (pacer.onFrame(recNs) != SnapTray::FramePacer::Decision::Emit) continue;
        qint64 ms = SnapTray::SessionClock::toEncoderMs(recNs);
        if (cutoffMs >= 0 && ms >= cutoffMs) continue;
        if (s.mutation == Mutation::ReorderTimestamps && ++written % 10 == 0) ms = std::max<qint64>(0, ms - qint64(2 * interval));
        const QImage image = makeFrame(s.size, e.id, e.flash, e.motion);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer retry;
        retry.start();
        do {
            encoder->writeFrame(image, ms);
            if (encoder->framesWritten() != before) break;
            QThread::msleep(2); // backpressure: retry the same frame
        } while (retry.elapsed() < kWriteRetryMs);
        if (encoder->framesWritten() == before) ++encoderRejected;
        else ++framesWritten;
        if (s.slowEncoderMs > 0 && ms / 1000 != lastStall) {
            lastStall = ms / 1000;
            QThread::msleep(ulong(s.slowEncoderMs));
        }
    }
    runAudioUntil(s.durationMs + kAudioChunkMs + kAudioLatencyMs);
    runControlsUntil(s.durationMs);
    clock.stop();
    if (withAudio) audio.flush(s.durationMs);
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty()) finished.wait(30000);
    const bool finishedOk = !finished.isEmpty() && finished.first().at(0).toBool();
    if (!finishedOk) result.failures << QStringLiteral("encoder did not finish: %1").arg(encoder->lastError());

    result.cost = {{"wallMs", wall.elapsed()},
                   {"cpuMs", qint64(double(std::clock() - cpuStart) * 1000 / CLOCKS_PER_SEC)},
                   {"rssKiB", residentKiB()},
                   {"fileBytes", QFileInfo(output).size()},
                   {"framesWritten", framesWritten},
                   {"encoderRejected", encoderRejected},
                   {"audioFramesWritten", audio.writtenFrames}};

    // Expected, from the reference model.
    QJsonArray expectedFlashes;
    for (const auto& f : expected) if (f.flash) expectedFlashes.append(f.ms);
    int expectedUnique = 0;
    for (size_t i = 0; i < expected.size(); ++i) expectedUnique += (i == 0 || expected[i].id != expected[i - 1].id) ? 1 : 0;
    result.expected = {{"frames", int(expected.size())}, {"uniqueFrames", expectedUnique},
                       {"firstId", expected.empty() ? -1 : expected.front().id},
                       {"lastId", expected.empty() ? -1 : expected.back().id},
                       {"lastFrameMs", lastFrameMs}, {"flashesMs", expectedFlashes},
                       {"durationMs", recordingMs(s, s.durationMs)}};
    if (!finishedOk) return result;

    // Decode what was actually written.
    QString videoError;
    const auto decoded = decodeVideo(output, &videoError);
    if (!videoError.isEmpty()) result.failures << QStringLiteral("video: %1").arg(videoError);
    if (decoded.empty()) {
        result.failures << QStringLiteral("no decodable video frames");
        return result;
    }
    // Identity: every expected frame present, in order, at its time; nothing else.
    std::vector<int> expectedIds, decodedIds;
    for (const auto& f : expected) expectedIds.push_back(f.id);
    for (const auto& f : decoded) decodedIds.push_back(f.id);
    int unique = 0, repeated = 0, outOfOrder = 0;
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (i == 0 || decoded[i].id != decoded[i - 1].id) ++unique; else ++repeated;
        if (i > 0 && decoded[i].id < decoded[i - 1].id) ++outOfOrder;
    }
    QSet<int> decodedSet(decodedIds.begin(), decodedIds.end());
    QSet<int> expectedSet(expectedIds.begin(), expectedIds.end());
    const QSet<int> missing = expectedSet - decodedSet;
    const QSet<int> unexpected = decodedSet - expectedSet;
    if (!missing.isEmpty()) result.failures << QStringLiteral("%1 expected frames missing").arg(missing.size());
    if (!unexpected.isEmpty()) {
        result.failures << QStringLiteral("%1 frames that should not be there (e.g. from a pause)").arg(unexpected.size());
    }
    if (outOfOrder) result.failures << QStringLiteral("%1 frames out of order").arg(outOfOrder);
    // Placement: every decoded frame is presented when that frame was
    // expected (repeats of a still screen match their nearest occurrence).
    QMultiHash<int, qint64> expectedTimes;
    for (const auto& f : expected) expectedTimes.insert(f.id, f.ms);
    int misplaced = 0;
    const double placementTolerance = std::max(2.0, interval / 2);
    for (const auto& d : decoded) {
        qint64 nearest = std::numeric_limits<qint64>::max();
        for (const qint64 t : expectedTimes.values(d.id)) nearest = std::min(nearest, qAbs(t - d.ms));
        if (expectedTimes.contains(d.id) && nearest > placementTolerance) ++misplaced;
    }
    if (misplaced) result.failures << QStringLiteral("%1 frames shown at the wrong time").arg(misplaced);
    // The tail is complete.
    const qint64 lastDecodedMs = decoded.back().ms;
    if (!expected.empty() && decoded.back().id != expected.back().id) {
        result.failures << QStringLiteral("tail cut: last frame %1, expected %2").arg(decoded.back().id).arg(expected.back().id);
    }
    if (!expected.empty() && lastDecodedMs + interval < lastFrameMs) {
        result.failures << QStringLiteral("video ends at %1 ms, expected %2 ms").arg(lastDecodedMs).arg(lastFrameMs);
    }
    // Pauses are removed: no frame from inside one, no gap where it was.
    for (const auto& p : s.pauses) {
        const qint64 at = recordingMs(s, p.atMs);
        for (size_t i = 1; i < decoded.size(); ++i) {
            if (decoded[i - 1].ms < at && decoded[i].ms >= at && decoded[i].ms - decoded[i - 1].ms > 3 * interval) {
                result.failures << QStringLiteral("a %1 ms gap where the pause at %2 ms was")
                                       .arg(decoded[i].ms - decoded[i - 1].ms).arg(p.atMs);
            }
        }
    }
    // A flash is one frame; consecutive flash frames (a repeated frame) are
    // one flash. Flashes on both sides of a removed pause stay separate.
    QJsonArray decodedFlashes;
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i].flash && (i == 0 || !decoded[i - 1].flash)) decodedFlashes.append(decoded[i].ms);
    }
    result.measured = {{"frames", int(decoded.size())}, {"uniqueFrames", unique}, {"repeatedFrames", repeated},
                       {"firstId", decoded.front().id}, {"lastId", decoded.back().id},
                       {"lastFrameMs", lastDecodedMs}, {"missing", missing.size()}, {"unexpected", unexpected.size()},
                       {"outOfOrder", outOfOrder}, {"misplaced", misplaced}, {"flashesMs", decodedFlashes},
                       {"uniqueFps", lastDecodedMs > 0 ? unique * 1000.0 / lastDecodedMs : 0.0}};
    if (decodedFlashes.size() != expectedFlashes.size()) {
        result.failures << QStringLiteral("%1 flashes decoded, %2 expected").arg(decodedFlashes.size()).arg(expectedFlashes.size());
    }

    // Audio: present when asked for, covering the video, pulses on the flashes.
    if (!withAudio) return result;
    qint64 audioLengthMs = 0;
    QString audioError;
    const auto onsets = decodePulses(output, &audioLengthMs, &audioError);
    if (!audioError.isEmpty()) {
        result.failures << QStringLiteral("audio: %1").arg(audioError);
        return result;
    }
    QJsonArray offsets;
    double worst = 0;
    for (const auto& flash : decodedFlashes) {
        const qint64 v = flash.toInteger();
        qint64 best = std::numeric_limits<qint64>::max();
        for (const qint64 a : onsets) if (qAbs(a - v) < qAbs(best)) best = a - v;
        if (best == std::numeric_limits<qint64>::max()) continue;
        offsets.append(best);
        worst = std::max(worst, double(qAbs(best)));
    }
    const double tolerance = interval + kAvToleranceMs;
    if (offsets.size() < decodedFlashes.size()) result.failures << QStringLiteral("flashes without an audio pulse");
    if (worst > tolerance) {
        result.failures << QStringLiteral("audio/video offset up to %1 ms (tolerance %2 ms)").arg(worst).arg(tolerance);
    }
    // Audio is cut short when it ends well before the video does.
    if (audioLengthMs + qint64(tolerance) < lastDecodedMs) {
        result.failures << QStringLiteral("audio ends at %1 ms, video at %2 ms").arg(audioLengthMs).arg(lastDecodedMs);
    }
    result.measured.insert(QStringLiteral("audioMs"), audioLengthMs);
    result.measured.insert(QStringLiteral("pulseOffsetsMs"), offsets);
    result.measured.insert(QStringLiteral("worstAvOffsetMs"), worst);
    result.measured.insert(QStringLiteral("audioPulses"), int(onsets.size()));
    return result;
}

} // namespace SyncMatrix
