#pragma once

#include <QByteArray>
#include <QFile>
#include <QJsonObject>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <memory>

// Microphone and system audio as separate tracks (#189). While recording
// with both sources, each is also written on its own next to the temporary
// recording ("<video>.mic.pcm", "<video>.system.pcm"), on the same 48 kHz
// timeline as the mixed track in the video, so they line up frame for frame.
// The video keeps its mixed track: older players, the unedited fast path and
// recordings made without separate tracks are unchanged. After recording the
// preview and exports mix the tracks again at the chosen volumes.
//
// File: a 16-byte header ("SNAPPCM1", rate, channels), then interleaved
// little-endian PCM16; frame f is at header + f * 4 bytes. Gaps read as
// silence, and a crash leaves every frame written before it readable.
namespace SnapTray {

namespace AudioTracks {

enum class Track { Microphone, SystemAudio };
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kBytesPerFrame = kChannels * 2;
constexpr auto kMicrophoneSuffix = ".mic.pcm";
constexpr auto kSystemAudioSuffix = ".system.pcm";

QString pathFor(const QString& videoPath, Track track);
// Both tracks are there (a recording made with separate tracks).
bool exist(const QString& videoPath);
void remove(const QString& videoPath);

} // namespace AudioTracks

// Appends one track while recording; thread-safe (the capture thread writes).
class AudioTrackWriter {
public:
    explicit AudioTrackWriter(QString path);
    ~AudioTrackWriter();
    bool open();
    void write(const QByteArray& pcm, qint64 startFrame);
    void flush();
    void close();
    bool failed() const;
    // Tests: as if a write had failed (a full disk).
    void failForTests();

private:
    friend struct AudioTrackWriterTestAccess;
    void flushLocked(); // caller holds m_mutex

    mutable QMutex m_mutex;
    QFile m_file;
    bool m_failed = false;
    int m_chunksSinceFlush = 0;
};

// Reads a track at any frame; past its end is silence. The track can be
// moved or removed while it is open (a save while the preview plays it).
class AudioTrackReader {
public:
    bool open(const QString& path);
    bool isOpen() const { return m_file.isOpen(); }
    QByteArray read(qint64 startFrame, int frames);
    qint64 frames() const;

private:
    QFile m_file;
};

// The mix after recording: each track's volume, mute, and noise reduction
// for the microphone. Neutral = exactly the recorded mix.
struct AudioMixSettings {
    double microphoneGainDb = 0.0;
    double systemAudioGainDb = 0.0;
    bool microphoneMuted = false;
    bool systemAudioMuted = false;
    bool microphoneNoiseReduction = false;

    static constexpr double kMinGainDb = -24.0;
    static constexpr double kMaxGainDb = 12.0;

    bool isNeutral() const;
    bool operator==(const AudioMixSettings& o) const;
    bool operator!=(const AudioMixSettings& o) const { return !(*this == o); }
    QJsonObject toJson() const;
    static AudioMixSettings fromJson(const QJsonObject& json);
};

// Cleans up a microphone track: an 80 Hz high-pass (rumble, desk knocks)
// and a soft noise gate that turns down what stays below a speech level
// (hiss, fans), with smooth attack and release. Streaming and deterministic;
// a few ms to settle after a seek.
class MicrophoneNoiseReducer {
public:
    void reset();
    void process(QByteArray& pcm); // interleaved stereo PCM16, in place

    static constexpr double kHighPassHz = 80.0;
    static constexpr double kGateThresholdDb = -45.0;
    static constexpr double kGateReductionDb = -20.0;

private:
    double m_x1[2] = {0, 0}, m_y1[2] = {0, 0};
    double m_envelope = 0.0;
    double m_gain = 1.0;
};

// Mixes the two tracks with the settings; reads from the track files.
class AudioTrackMixer {
public:
    AudioTrackMixer(const QString& videoPath, const AudioMixSettings& settings);
    bool isValid() const { return m_microphone.isOpen() && m_systemAudio.isOpen(); }
    // Interleaved stereo PCM16 for [startFrame, startFrame + frames).
    QByteArray read(qint64 startFrame, int frames);
    void setSettings(const AudioMixSettings& settings);

private:
    AudioTrackReader m_microphone;
    AudioTrackReader m_systemAudio;
    AudioMixSettings m_settings;
    MicrophoneNoiseReducer m_reducer;
    qint64 m_nextFrame = -1;
};

} // namespace SnapTray
