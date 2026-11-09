#ifndef RECORDINGAUDIOSTATUS_H
#define RECORDINGAUDIOSTATUS_H

#include <QMetaType>
#include <QString>

namespace SnapTray {

// What happened to one audio source (microphone or system audio) for the
// current recording. "Requested" states come from settings; the others are
// set from actual permission, capture and encoder results.
enum class AudioSourceState {
    Off,               // Not requested
    Starting,          // Requested; capture not confirmed yet
    Active,            // Capture started
    PermissionDenied,  // OS permission refused
    DeviceMissing,     // Selected device disconnected or not found
    Unavailable,       // Capture or encoding failed for another reason
    UnsupportedFormat, // The output format cannot carry audio (GIF/WebP)
};

struct RecordingAudioStatus {
    AudioSourceState microphone = AudioSourceState::Off;
    AudioSourceState systemAudio = AudioSourceState::Off;

    bool operator==(const RecordingAudioStatus& other) const
    {
        return microphone == other.microphone && systemAudio == other.systemAudio;
    }
    bool operator!=(const RecordingAudioStatus& other) const { return !(*this == other); }

    bool anyRequested() const
    {
        return microphone != AudioSourceState::Off || systemAudio != AudioSourceState::Off;
    }
    bool anyActive() const
    {
        return microphone == AudioSourceState::Active || systemAudio == AudioSourceState::Active;
    }
};

// Audio-source setting values (RecordingSettingsManager::audioSource).
inline constexpr int kAudioSourceMicrophone = 0;
inline constexpr int kAudioSourceSystem = 1;
inline constexpr int kAudioSourceBoth = 2;

// Whether the file being recorded can hold audio. With preview on the
// recording is always an MP4; without preview GIF and WebP are written
// directly and have no audio track.
bool recordingCapturesAudio(int outputFormat, bool showPreview);

// Initial status for a recording request, before permissions and capture.
RecordingAudioStatus requestedAudioStatus(bool audioEnabled, int audioSource,
                                          int outputFormat, bool showPreview);

// Short, translated description of one source's state, e.g. "Permission denied".
QString audioSourceStateText(AudioSourceState state);

// One line for the tray tooltip, e.g. "Microphone: Active · System audio: Off".
// Empty when no audio was requested.
QString describeRecordingAudioStatus(const RecordingAudioStatus& status);

} // namespace SnapTray

Q_DECLARE_METATYPE(SnapTray::RecordingAudioStatus)

#endif // RECORDINGAUDIOSTATUS_H
