#include "recording/AudioTracks.h"

#include <QDebug>
#include <QFileInfo>
#include <QMutexLocker>
#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace SnapTray {

namespace {

constexpr char kMagic[8] = {'S', 'N', 'A', 'P', 'P', 'C', 'M', '1'};
constexpr qint64 kHeaderBytes = 16;
constexpr qint64 kZeroFillChunk = 64 * 1024;
constexpr double kAttackMs = 5.0;
constexpr double kReleaseMs = 100.0;
constexpr double kGateOpenMs = 10.0;
constexpr double kGateCloseMs = 150.0;
constexpr double kPcmFullScale = 32768.0;
constexpr double kPi = 3.14159265358979323846;
// A crash loses at most about a second: the file is flushed this often.
constexpr int kFlushEveryChunks = 100;

double coefficient(double ms)
{
    return std::exp(-1000.0 / (ms * AudioTracks::kSampleRate));
}

double gainOf(double db, bool muted)
{
    return muted ? 0.0 : std::pow(10.0, db / 20.0);
}

qint16 clampSample(double value)
{
    return qint16(std::clamp(std::lround(value), long(-32768), long(32767)));
}

} // namespace

namespace AudioTracks {

QString pathFor(const QString& videoPath, Track track)
{
    return videoPath + QLatin1String(track == Track::Microphone ? kMicrophoneSuffix : kSystemAudioSuffix);
}

bool exist(const QString& videoPath)
{
    return QFileInfo::exists(pathFor(videoPath, Track::Microphone))
        && QFileInfo::exists(pathFor(videoPath, Track::SystemAudio));
}

void remove(const QString& videoPath)
{
    for (const Track track : {Track::Microphone, Track::SystemAudio}) {
        const QString path = pathFor(videoPath, track);
        if (QFileInfo::exists(path)) QFile::remove(path);
    }
}

} // namespace AudioTracks

AudioTrackWriter::AudioTrackWriter(QString path)
    : m_file(std::move(path))
{
}

AudioTrackWriter::~AudioTrackWriter()
{
    close();
}

bool AudioTrackWriter::open()
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AudioTrackWriter: cannot open" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        return false;
    }
    QByteArray header(kMagic, sizeof(kMagic));
    header.resize(kHeaderBytes);
    qToLittleEndian<qint32>(AudioTracks::kSampleRate, header.data() + 8);
    qToLittleEndian<qint32>(AudioTracks::kChannels, header.data() + 12);
    if (m_file.write(header) != header.size()) {
        m_failed = true;
        return false;
    }
    return true;
}

void AudioTrackWriter::write(const QByteArray& pcm, qint64 startFrame)
{
    QMutexLocker locker(&m_mutex);
    if (m_failed || !m_file.isOpen() || pcm.isEmpty() || startFrame < 0) return;
    const qint64 at = kHeaderBytes + startFrame * AudioTracks::kBytesPerFrame;
    // Chunks arrive in order; a gap (a long silence the mixer skipped) is
    // filled with zeros explicitly, never left to the file system.
    if (at > m_file.size()) {
        m_file.seek(m_file.size());
        const QByteArray zeros(kZeroFillChunk, '\0');
        for (qint64 missing = at - m_file.size(); missing > 0;) {
            const qint64 n = qMin(missing, kZeroFillChunk);
            if (m_file.write(zeros.constData(), n) != n) {
                m_failed = true;
                return;
            }
            missing -= n;
        }
    }
    if (!m_file.seek(at) || m_file.write(pcm) != pcm.size()) {
        qWarning() << "AudioTrackWriter: cannot write" << m_file.fileName() << m_file.errorString();
        m_failed = true;
        return;
    }
    if (++m_chunksSinceFlush >= kFlushEveryChunks) {
        m_chunksSinceFlush = 0;
        m_file.flush();
    }
}

void AudioTrackWriter::flush()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) m_file.flush();
}

void AudioTrackWriter::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) m_file.close();
}

bool AudioTrackWriter::failed() const
{
    QMutexLocker locker(&m_mutex);
    return m_failed;
}

void AudioTrackWriter::failForTests()
{
    QMutexLocker locker(&m_mutex);
    m_failed = true;
}

bool AudioTrackReader::open(const QString& path)
{
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) return false;
    const QByteArray header = m_file.read(kHeaderBytes);
    if (header.size() != kHeaderBytes || !header.startsWith(QByteArray(kMagic, sizeof(kMagic)))
        || qFromLittleEndian<qint32>(header.constData() + 8) != AudioTracks::kSampleRate
        || qFromLittleEndian<qint32>(header.constData() + 12) != AudioTracks::kChannels) {
        m_file.close();
        return false;
    }
    return true;
}

qint64 AudioTrackReader::frames() const
{
    return m_file.isOpen() ? qMax<qint64>(0, (m_file.size() - kHeaderBytes) / AudioTracks::kBytesPerFrame) : 0;
}

QByteArray AudioTrackReader::read(qint64 startFrame, int frames)
{
    QByteArray out(qMax(0, frames) * AudioTracks::kBytesPerFrame, '\0');
    if (!m_file.isOpen() || frames <= 0) return out;
    const qint64 available = frames - qMax<qint64>(0, -startFrame);
    const qint64 from = qMax<qint64>(0, startFrame);
    if (available <= 0 || from >= this->frames()) return out;
    const qint64 count = qMin<qint64>(available, this->frames() - from);
    if (!m_file.seek(kHeaderBytes + from * AudioTracks::kBytesPerFrame)) return out;
    const QByteArray data = m_file.read(count * AudioTracks::kBytesPerFrame);
    const qint64 offset = (from - startFrame) * AudioTracks::kBytesPerFrame;
    std::copy(data.constBegin(), data.constEnd(), out.begin() + offset);
    return out;
}

bool AudioMixSettings::isNeutral() const
{
    return qFuzzyIsNull(microphoneGainDb) && qFuzzyIsNull(systemAudioGainDb) && !microphoneMuted && !systemAudioMuted
        && !microphoneNoiseReduction;
}

bool AudioMixSettings::operator==(const AudioMixSettings& o) const
{
    return qFuzzyCompare(microphoneGainDb + 100.0, o.microphoneGainDb + 100.0)
        && qFuzzyCompare(systemAudioGainDb + 100.0, o.systemAudioGainDb + 100.0)
        && microphoneMuted == o.microphoneMuted && systemAudioMuted == o.systemAudioMuted
        && microphoneNoiseReduction == o.microphoneNoiseReduction;
}

QJsonObject AudioMixSettings::toJson() const
{
    return {{QStringLiteral("microphoneGainDb"), microphoneGainDb},
            {QStringLiteral("systemAudioGainDb"), systemAudioGainDb},
            {QStringLiteral("microphoneMuted"), microphoneMuted},
            {QStringLiteral("systemAudioMuted"), systemAudioMuted},
            {QStringLiteral("microphoneNoiseReduction"), microphoneNoiseReduction}};
}

AudioMixSettings AudioMixSettings::fromJson(const QJsonObject& json)
{
    AudioMixSettings s;
    s.microphoneGainDb = std::clamp(json.value(QStringLiteral("microphoneGainDb")).toDouble(0.0), kMinGainDb, kMaxGainDb);
    s.systemAudioGainDb = std::clamp(json.value(QStringLiteral("systemAudioGainDb")).toDouble(0.0), kMinGainDb, kMaxGainDb);
    s.microphoneMuted = json.value(QStringLiteral("microphoneMuted")).toBool(false);
    s.systemAudioMuted = json.value(QStringLiteral("systemAudioMuted")).toBool(false);
    s.microphoneNoiseReduction = json.value(QStringLiteral("microphoneNoiseReduction")).toBool(false);
    return s;
}

void MicrophoneNoiseReducer::reset()
{
    *this = MicrophoneNoiseReducer();
}

void MicrophoneNoiseReducer::process(QByteArray& pcm)
{
    const double dt = 1.0 / AudioTracks::kSampleRate;
    const double rc = 1.0 / (2.0 * kPi * kHighPassHz);
    const double alpha = rc / (rc + dt);
    const double attack = coefficient(kAttackMs), release = coefficient(kReleaseMs);
    const double open = coefficient(kGateOpenMs), close = coefficient(kGateCloseMs);
    const double threshold = kPcmFullScale * std::pow(10.0, kGateThresholdDb / 20.0);
    const double reduced = std::pow(10.0, kGateReductionDb / 20.0);
    auto* samples = reinterpret_cast<qint16*>(pcm.data());
    const qsizetype frames = pcm.size() / AudioTracks::kBytesPerFrame;
    for (qsizetype f = 0; f < frames; ++f) {
        double filtered[2];
        for (int c = 0; c < 2; ++c) {
            const double x = qFromLittleEndian<qint16>(samples + f * 2 + c);
            m_y1[c] = alpha * (m_y1[c] + x - m_x1[c]);
            m_x1[c] = x;
            filtered[c] = m_y1[c];
        }
        const double level = std::max(std::abs(filtered[0]), std::abs(filtered[1]));
        const double k = level > m_envelope ? attack : release;
        m_envelope = k * m_envelope + (1.0 - k) * level;
        const double target = m_envelope >= threshold ? 1.0 : reduced;
        const double g = target > m_gain ? open : close;
        m_gain = g * m_gain + (1.0 - g) * target;
        for (int c = 0; c < 2; ++c) {
            qToLittleEndian<qint16>(clampSample(filtered[c] * m_gain), samples + f * 2 + c);
        }
    }
}

AudioTrackMixer::AudioTrackMixer(const QString& videoPath, const AudioMixSettings& settings)
    : m_settings(settings)
{
    m_microphone.open(AudioTracks::pathFor(videoPath, AudioTracks::Track::Microphone));
    m_systemAudio.open(AudioTracks::pathFor(videoPath, AudioTracks::Track::SystemAudio));
}

void AudioTrackMixer::setSettings(const AudioMixSettings& settings)
{
    if (settings.microphoneNoiseReduction != m_settings.microphoneNoiseReduction) m_reducer.reset();
    m_settings = settings;
}

QByteArray AudioTrackMixer::read(qint64 startFrame, int frames)
{
    if (startFrame != m_nextFrame) m_reducer.reset(); // a seek: no state from elsewhere
    m_nextFrame = startFrame + frames;
    QByteArray microphone = m_microphone.read(startFrame, frames);
    const QByteArray system = m_systemAudio.read(startFrame, frames);
    if (m_settings.microphoneNoiseReduction && !m_settings.microphoneMuted) m_reducer.process(microphone);
    const double gm = gainOf(m_settings.microphoneGainDb, m_settings.microphoneMuted);
    const double gs = gainOf(m_settings.systemAudioGainDb, m_settings.systemAudioMuted);
    QByteArray out(microphone.size(), '\0');
    const auto* mic = reinterpret_cast<const qint16*>(microphone.constData());
    const auto* sys = reinterpret_cast<const qint16*>(system.constData());
    auto* mixed = reinterpret_cast<qint16*>(out.data());
    for (qsizetype i = 0; i < out.size() / 2; ++i) {
        const double value = qFromLittleEndian<qint16>(mic + i) * gm + qFromLittleEndian<qint16>(sys + i) * gs;
        qToLittleEndian<qint16>(clampSample(value), mixed + i);
    }
    return out;
}

} // namespace SnapTray
