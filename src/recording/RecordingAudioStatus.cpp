#include "recording/RecordingAudioStatus.h"

#include <QCoreApplication>
#include <QStringList>

namespace SnapTray {

namespace {

constexpr auto kContext = "RecordingAudioStatus";
constexpr int kOutputFormatMp4 = 0;

struct StateText {
    AudioSourceState state;
    const char* text;
};

// Lookup table rather than a switch so every state has exactly one label.
constexpr StateText kStateTexts[] = {
    {AudioSourceState::Off, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Off")},
    {AudioSourceState::Starting, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Starting")},
    {AudioSourceState::Active, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Recording")},
    {AudioSourceState::PermissionDenied, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Permission denied")},
    {AudioSourceState::DeviceMissing, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Device not found")},
    {AudioSourceState::Unavailable, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Unavailable")},
    {AudioSourceState::UnsupportedFormat, QT_TRANSLATE_NOOP("RecordingAudioStatus", "Not supported by GIF/WebP")},
};

} // namespace

bool recordingCapturesAudio(int outputFormat, bool showPreview)
{
    return showPreview || outputFormat == kOutputFormatMp4;
}

RecordingAudioStatus requestedAudioStatus(bool audioEnabled, int audioSource,
                                          int outputFormat, bool showPreview)
{
    RecordingAudioStatus status;
    if (!audioEnabled) {
        return status;
    }
    const auto requested = recordingCapturesAudio(outputFormat, showPreview)
        ? AudioSourceState::Starting
        : AudioSourceState::UnsupportedFormat;
    if (audioSource == kAudioSourceMicrophone || audioSource == kAudioSourceBoth) {
        status.microphone = requested;
    }
    if (audioSource == kAudioSourceSystem || audioSource == kAudioSourceBoth) {
        status.systemAudio = requested;
    }
    return status;
}

QString audioSourceStateText(AudioSourceState state)
{
    for (const auto& entry : kStateTexts) {
        if (entry.state == state) {
            return QCoreApplication::translate(kContext, entry.text);
        }
    }
    return {};
}

QString describeRecordingAudioStatus(const RecordingAudioStatus& status)
{
    if (!status.anyRequested()) {
        return {};
    }
    const QString pattern = QCoreApplication::translate(kContext, "%1: %2");
    QStringList parts;
    parts.append(pattern.arg(QCoreApplication::translate(kContext, "Microphone"),
                             audioSourceStateText(status.microphone)));
    parts.append(pattern.arg(QCoreApplication::translate(kContext, "System audio"),
                             audioSourceStateText(status.systemAudio)));
    return parts.join(QStringLiteral(" · "));
}

} // namespace SnapTray
