#include "SyncMatrix.h"

#include "IVideoEncoder.h"
#include "capture/TimestampedPcmMixer.h"
#include "recording/FramePacer.h"
#include "recording/SessionClock.h"
#include "video/FFmpegMedia.h"

#include <deque>
#include <QHash>
#include "encoding/EncodingWorker.h"
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
#include <limits>
#include <cmath>
#include <ctime>
#include <memory>

namespace SyncMatrix {

namespace {

using SnapTray::Audio::Source;
using SnapTray::Audio::TimestampedPcmMixer;

constexpr qint64 kNsPerMs = 1000000;
// Frame ID barcode: two rows of 16 cells, 32 bits, so long rows (an hour at
// 60 fps is 216000 frames) never wrap (#178 review). Both rows sit above the
// flash area, which starts a quarter of the way down.
constexpr int kIdCellsPerRow = 16;
constexpr int kIdRows = 2;
constexpr int kIdBits = kIdCellsPerRow * kIdRows;
constexpr qint64 kFlashFirstMs = 350;    // flash + audio pulse schedule (wall clock)
constexpr qint64 kFlashEveryMs = 700;
constexpr qint64 kPulseMs = 40;
// Each source pulses at its own pitch, so a mixed recording shows both
// (#178 review). 5 ms windows hold whole cycles of each.
constexpr double kMicrophonePulseHz = 1000.0;
constexpr double kSystemAudioPulseHz = 3000.0;
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr qint64 kAudioChunkMs = 10;      // capture callback period
constexpr qint64 kAudioLatencyMs = 5;     // callback arrives after its last sample
constexpr qint64 kTruncateTailMs = 500;   // TruncateTail drops this much at the end
constexpr qint64 kShiftAudioMs = 120;     // ShiftAudio moves audio this much
constexpr qint64 kWriteRetryMs = 2000;    // an encoder applying backpressure
constexpr int kPulseWindowFrames = 240;   // 5 ms analysis windows
constexpr double kPulseThreshold = 0.08;  // amplitude (full scale 1.0) of a source's pitch that counts as its pulse

// --- Synthetic content -----------------------------------------------------

QRect idCell(const QSize& size, int bit)
{
    const int band = size.height() / 8;
    const int cell = size.width() / kIdCellsPerRow;
    return QRect((bit % kIdCellsPerRow) * cell, (bit / kIdCellsPerRow) * band, cell, band);
}

} // namespace

QImage makeFrame(const QSize& size, int id, bool flash, int motion)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(64, 64, 64));
    QPainter painter(&image);
    const auto bits = static_cast<quint32>(id);
    for (int bit = 0; bit < kIdBits; ++bit) {
        painter.fillRect(idCell(size, bit), (bits >> bit) & 1u ? Qt::white : Qt::black);
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

namespace {

// Distance from `t` to the closest of the sorted `times` (not empty).
qint64 nearestDistance(const std::vector<qint64>& times, qint64 t)
{
    const auto after = std::lower_bound(times.begin(), times.end(), t);
    qint64 nearest = std::numeric_limits<qint64>::max();
    if (after != times.end()) nearest = *after - t;
    if (after != times.begin()) nearest = std::min(nearest, t - *(after - 1));
    return nearest;
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

} // namespace

bool flashActiveAt(qint64 wallMs, qint64 lengthMs)
{
    // Constant time: called for every audio sample, so scanning the schedule
    // made long rows quadratic (#178 review). Flashes are shorter than their
    // spacing, so only the latest one can cover wallMs.
    static_assert(kPulseMs < kFlashEveryMs, "a pulse must end before the next one");
    if (wallMs < kFlashFirstMs) return false;
    return (wallMs - kFlashFirstMs) % kFlashEveryMs < std::min(lengthMs, kFlashEveryMs);
}

int readId(const QImage& frame)
{
    quint32 bits = 0;
    for (int bit = 0; bit < kIdBits; ++bit) {
        // The middle half of each cell, away from codec ringing at its edges.
        const QRect cell = idCell(frame.size(), bit);
        const QRect probe(cell.x() + cell.width() / 4, cell.y() + cell.height() / 4,
                          std::max(1, cell.width() / 2), std::max(1, cell.height() / 2));
        if (meanGray(frame, probe) > 128) bits |= 1u << bit;
    }
    return static_cast<int>(bits);
}

namespace {

bool isFlash(const QImage& frame)
{
    return meanGray(frame, QRect(0, frame.height() / 4, frame.width(), frame.height() * 3 / 4)) > 200;
}

bool pulseAt(qint64 wallMs, double* phaseOut, qint64 sampleIndex, double hz)
{
    if (!flashActiveAt(wallMs, kPulseMs)) return false;
    *phaseOut = 2.0 * M_PI * hz * double(sampleIndex) / kSampleRate;
    return true;
}

QByteArray audioChunk(qint64 wallStartMs, qint64 lengthMs, double amplitude, double hz)
{
    const int frames = int(lengthMs * kSampleRate / 1000);
    QByteArray pcm(frames * kChannels * 2, '\0');
    auto* out = reinterpret_cast<qint16*>(pcm.data());
    for (int i = 0; i < frames; ++i) {
        const qint64 sample = wallStartMs * kSampleRate / 1000 + i;
        const qint64 wallMs = sample * 1000 / kSampleRate;
        double phase = 0;
        const qint16 v = pulseAt(wallMs, &phase, sample, hz) ? qint16(amplitude * 32767 * std::sin(phase)) : 0;
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
        // Sorted: a binary search keeps long rows linear (#178 review).
        const bool flash = !still && std::binary_search(flashFrames.begin(), flashFrames.end(), i);
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
// The encoding worker's frame queue while the encoder is unavailable: the
// same budget and refusal rule (a new frame is refused when the queue is
// full or its oldest frame too old), on the simulated clock. Stalls recur
// every kStallPeriodMs for slowEncoderMs.
constexpr qint64 kStallPeriodMs = 2000;

struct EncoderQueueModel {
    const Scenario& s;
    const EncodingWorker::QueueBudget budget{};
    struct Queued { qint64 ms; int id; bool flash; int motion; double enqueuedMs; };
    std::deque<Queued> queue;
    qint64 dropped = 0;
    int maxDepth = 0;

    bool stalled(double wallMs) const
    {
        return s.slowEncoderMs > 0 && std::fmod(wallMs, double(kStallPeriodMs)) < s.slowEncoderMs;
    }
    // Frames the encoder takes now (in order), given a new one arriving at wallMs.
    template <typename Write>
    void offer(double wallMs, const Queued& frame, Write&& write)
    {
        if (!stalled(wallMs)) {
            drain(write);
            write(frame);
            return;
        }
        if (int(queue.size()) >= budget.maxFrames
            || (!queue.empty() && wallMs - queue.front().enqueuedMs > budget.maxAgeMs)) {
            ++dropped; // refused, like EncodingWorker's QueueFull / stale
            return;
        }
        queue.push_back(frame);
        maxDepth = std::max(maxDepth, int(queue.size()));
    }
    template <typename Write>
    void drain(Write&& write)
    {
        while (!queue.empty()) {
            write(queue.front());
            queue.pop_front();
        }
    }
};

// The pacing rule as FramePacer.h specifies it, written out for the
// reference model: a frame closer than 60 % of an interval to the last one
// kept is dropped, nothing else is. Independent of FramePacer, so a bug in
// it shows as missing frames instead of shrinking the expectation too
// (#178 review).
class SpecPacer
{
public:
    explicit SpecPacer(int fps) : m_intervalNs(1000000000LL / std::max(1, fps)) {}
    bool keep(qint64 recordingNs)
    {
        if (m_lastNs >= 0 && recordingNs - m_lastNs < m_intervalNs * 60 / 100) return false;
        m_lastNs = recordingNs;
        return true;
    }

private:
    qint64 m_intervalNs;
    qint64 m_lastNs = -1;
};

// Every FramePacer the matrix builds; BrokenPacer simulates a bug in it.
SnapTray::FramePacer makePacer(const Scenario& s)
{
    return SnapTray::FramePacer(s.mutation == Mutation::BrokenPacer ? s.fps / 2 : s.fps);
}

std::vector<ExpectedFrame> expectedFrames(const Scenario& s, const std::vector<VideoEvent>& events)
{
    SnapTray::SessionClock clock;
    SpecPacer pacer(s.fps);
    clock.start(0);
    auto ctl = controls(s);
    size_t next = 0;
    std::vector<ExpectedFrame> frames;
    EncoderQueueModel queue{s};
    for (const auto& e : events) {
        while (next < ctl.size() && ctl[next].wallMs <= e.arrivalMs) {
            if (ctl[next].pause) clock.pause(qint64(ctl[next].wallMs * kNsPerMs));
            else clock.resume(qint64(ctl[next].wallMs * kNsPerMs));
            ++next;
        }
        qint64 recNs = 0;
        if (clock.admit(qint64(e.sourceMs * kNsPerMs), &recNs) != SnapTray::SessionClock::Admission::Accepted) continue;
        if (!pacer.keep(recNs)) continue;
        queue.offer(e.arrivalMs, {SnapTray::SessionClock::toEncoderMs(recNs), e.id, e.flash, e.motion, e.arrivalMs},
                    [&frames](const EncoderQueueModel::Queued& q) { frames.push_back({q.ms, q.id, q.flash}); });
    }
    queue.drain([&frames](const EncoderQueueModel::Queued& q) { frames.push_back({q.ms, q.id, q.flash}); });
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
            // ShiftMicrophone: the microphone's sound arrives late, alone.
            const double soundMs = s.mutation == Mutation::ShiftMicrophone ? startMs - kShiftAudioMs : startMs;
            deliver(mixer.push(Source::Microphone, {audioChunk(qint64(soundMs), kAudioChunkMs, 0.5, kMicrophonePulseHz), startNs, format}, nowNs));
        }
        if (hasSource(Source::SystemAudio) && s.mutation != Mutation::DropSystemAudio) {
            deliver(mixer.push(Source::SystemAudio, {audioChunk(qint64(startMs), kAudioChunkMs, 0.3, kSystemAudioPulseHz), startNs, format}, nowNs));
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
// Amplitude of `hz` in a window of the left channel (Goertzel).
double toneAmplitude(const qint16* samples, int count, double hz)
{
    const double coefficient = 2.0 * std::cos(2.0 * M_PI * hz / kSampleRate);
    double previous = 0, beforePrevious = 0;
    for (int i = 0; i < count; ++i) {
        const double value = samples[i * kChannels] / 32768.0 + coefficient * previous - beforePrevious;
        beforePrevious = previous;
        previous = value;
    }
    const double power = previous * previous + beforePrevious * beforePrevious - coefficient * previous * beforePrevious;
    return 2.0 * std::sqrt(std::max(0.0, power)) / count;
}

struct PulseOnsets {
    std::vector<qint64> microphone;
    std::vector<qint64> systemAudio;
};

// Where each source's pulses start, told apart by pitch.
PulseOnsets decodePulses(const QString& path, qint64* lengthMs, QString* error)
{
    SnapTray::FFmpeg::AudioReader reader;
    PulseOnsets onsets;
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
    bool micSounding = false, systemSounding = false;
    qint64 frame = 0;
    constexpr int kChunk = 4800;
    while (frame < endFrame) {
        const QByteArray pcm = reader.read(frame, int(std::min<qint64>(kChunk, endFrame - frame)));
        if (pcm.isEmpty()) break;
        const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
        const int frames = int(pcm.size() / (kChannels * 2));
        for (int w = 0; w + kPulseWindowFrames <= frames; w += kPulseWindowFrames) {
            const qint64 atMs = (frame + w) * 1000 / kSampleRate;
            const qint16* window = s + w * kChannels;
            const bool mic = toneAmplitude(window, kPulseWindowFrames, kMicrophonePulseHz) > kPulseThreshold;
            const bool system = toneAmplitude(window, kPulseWindowFrames, kSystemAudioPulseHz) > kPulseThreshold;
            if (mic && !micSounding) onsets.microphone.push_back(atMs);
            if (system && !systemSounding) onsets.systemAudio.push_back(atMs);
            micSounding = mic;
            systemSounding = system;
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
    case Mutation::DropRepeats: return "drop-repeats";
    case Mutation::BrokenPacer: return "broken-pacer";
    case Mutation::ShiftMicrophone: return "shift-microphone";
    case Mutation::DropSystemAudio: return "drop-system-audio";
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

QJsonObject Result::toJson(const Scenario& s, bool mustPass) const
{
    return {{"scenario", s.toJson()}, {"passed", passed()}, {"mustPass", mustPass},
            {"testSucceeded", mustPass ? passed() : !passed()}, {"failures", QJsonArray::fromStringList(failures)},
            {"skipped", QJsonArray::fromStringList(skipped)}, {"expected", expected}, {"measured", measured},
            {"cost", cost}};
}

void writeReport(const Scenario& scenario, const Result& result, bool mustPass)
{
    const QString dir = qEnvironmentVariable("SNAPTRAY_SYNC_REPORT_DIR");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(scenario.name + QStringLiteral(".json")));
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(result.toJson(scenario, mustPass)).toJson());
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
    SnapTray::FramePacer pacer = makePacer(s);
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
    int lastWrittenId = -1;
    EncoderQueueModel encoderQueue{s};
    const auto write = [&](const EncoderQueueModel::Queued& q) {
        const QImage image = makeFrame(s.size, q.id, q.flash, q.motion);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer retry;
        retry.start();
        do {
            encoder->writeFrame(image, q.ms);
            if (encoder->framesWritten() != before) break;
            QThread::msleep(2); // backpressure: retry the same frame
        } while (retry.elapsed() < kWriteRetryMs);
        if (encoder->framesWritten() == before) ++encoderRejected;
        else ++framesWritten;
    };
    for (const auto& e : events) {
        runAudioUntil(e.arrivalMs);
        runControlsUntil(e.arrivalMs);
        qint64 recNs = 0;
        if (clock.admit(qint64(e.sourceMs * kNsPerMs), &recNs) != SnapTray::SessionClock::Admission::Accepted) continue;
        if (pacer.onFrame(recNs) != SnapTray::FramePacer::Decision::Emit) continue;
        qint64 ms = SnapTray::SessionClock::toEncoderMs(recNs);
        if (cutoffMs >= 0 && ms >= cutoffMs) continue;
        if (s.mutation == Mutation::ReorderTimestamps && ++written % 10 == 0) ms = std::max<qint64>(0, ms - qint64(2 * interval));
        // Bug: a still screen's repeated frames are dropped.
        if (s.mutation == Mutation::DropRepeats && e.id == lastWrittenId) continue;
        lastWrittenId = e.id;
        // A stalled encoder: frames wait in the worker's queue (or are
        // refused past its budget) while capture and audio carry on.
        encoderQueue.offer(e.arrivalMs, {ms, e.id, e.flash, e.motion, e.arrivalMs}, write);
    }
    encoderQueue.drain(write);
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
                   {"queueDropped", encoderQueue.dropped},
                   {"queueMaxDepth", encoderQueue.maxDepth},
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
    // Occurrences, not just IDs: a still screen repeats one ID many times,
    // and losing most of those repeats must count (#178 review).
    QHash<int, int> expectedCount, decodedCount;
    for (const int id : expectedIds) ++expectedCount[id];
    for (const int id : decodedIds) ++decodedCount[id];
    int missingCount = 0, unexpectedCount = 0;
    for (auto it = expectedCount.cbegin(); it != expectedCount.cend(); ++it) {
        missingCount += std::max(0, it.value() - decodedCount.value(it.key()));
    }
    for (auto it = decodedCount.cbegin(); it != decodedCount.cend(); ++it) {
        unexpectedCount += std::max(0, it.value() - expectedCount.value(it.key()));
    }
    if (missingCount) result.failures << QStringLiteral("%1 expected frames missing").arg(missingCount);
    if (unexpectedCount) {
        result.failures << QStringLiteral("%1 frames that should not be there (e.g. from a pause)").arg(unexpectedCount);
    }
    if (outOfOrder) result.failures << QStringLiteral("%1 frames out of order").arg(outOfOrder);
    // Placement: every decoded frame is presented when that frame was
    // expected (repeats of a still screen match their nearest occurrence).
    // Times per ID in order (expected is in time order), searched rather than
    // scanned: a long still span repeats one ID thousands of times.
    QHash<int, std::vector<qint64>> expectedTimes;
    for (const auto& f : expected) expectedTimes[f.id].push_back(f.ms);
    int misplaced = 0;
    const double placementTolerance = std::max(2.0, interval / 2);
    for (const auto& d : decoded) {
        const auto times = expectedTimes.constFind(d.id);
        if (times == expectedTimes.cend()) continue;
        if (nearestDistance(*times, d.ms) > placementTolerance) ++misplaced;
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
                       {"lastFrameMs", lastDecodedMs}, {"missing", missingCount}, {"unexpected", unexpectedCount},
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
    const double tolerance = interval + kAvToleranceMs;
    // Each enabled source on its own: in a mix, one late or missing source
    // must not hide behind the other (#178 review). A source is not expected
    // where the scenario silences it.
    double worst = 0;
    const auto checkSource = [&](const QString& name, const std::vector<qint64>& sourceOnsets,
                                 qint64 silentFromMs, qint64 silentToMs, const QString& offsetsKey) {
        QJsonArray offsets;
        int missing = 0;
        double sourceWorst = 0;
        for (const auto& flash : decodedFlashes) {
            const qint64 v = flash.toInteger();
            if (silentFromMs >= 0 && v >= silentFromMs - qint64(tolerance) && v < silentToMs + qint64(tolerance)) continue;
            if (sourceOnsets.empty()) {
                ++missing;
                continue;
            }
            // Onsets are in time order: the nearest is beside v (the earlier one on a tie).
            const auto after = std::lower_bound(sourceOnsets.begin(), sourceOnsets.end(), v);
            qint64 best = after != sourceOnsets.end() ? *after - v : std::numeric_limits<qint64>::max();
            if (after != sourceOnsets.begin() && qAbs(*(after - 1) - v) <= qAbs(best)) best = *(after - 1) - v;
            offsets.append(best);
            sourceWorst = std::max(sourceWorst, double(qAbs(best)));
        }
        if (missing) result.failures << QStringLiteral("%1: %2 flashes without its pulse").arg(name).arg(missing);
        if (sourceWorst > tolerance) {
            result.failures << QStringLiteral("%1: audio/video offset up to %2 ms (tolerance %3 ms)")
                                   .arg(name).arg(sourceWorst).arg(tolerance);
        }
        worst = std::max(worst, sourceWorst);
        result.measured.insert(offsetsKey, offsets);
    };
    const auto config = AudioPath::config(s);
    if (config.microphoneEnabled) {
        const qint64 gapFrom = s.audioGapFromMs >= 0 ? recordingMs(s, s.audioGapFromMs) : -1;
        const qint64 gapTo = s.audioGapFromMs >= 0 ? recordingMs(s, s.audioGapToMs) : -1;
        checkSource(QStringLiteral("microphone"), onsets.microphone, gapFrom, gapTo, QStringLiteral("pulseOffsetsMs"));
    }
    if (config.systemAudioEnabled) {
        checkSource(QStringLiteral("system audio"), onsets.systemAudio, -1, -1,
                    config.microphoneEnabled ? QStringLiteral("systemAudioPulseOffsetsMs")
                                             : QStringLiteral("pulseOffsetsMs"));
    }
    // Audio is cut short when it ends well before the video does.
    if (audioLengthMs + qint64(tolerance) < lastDecodedMs) {
        result.failures << QStringLiteral("audio ends at %1 ms, video at %2 ms").arg(audioLengthMs).arg(lastDecodedMs);
    }
    result.measured.insert(QStringLiteral("audioMs"), audioLengthMs);
    result.measured.insert(QStringLiteral("worstAvOffsetMs"), worst);
    result.measured.insert(QStringLiteral("microphonePulses"), int(onsets.microphone.size()));
    result.measured.insert(QStringLiteral("systemAudioPulses"), int(onsets.systemAudio.size()));
    return result;
}

} // namespace SyncMatrix
