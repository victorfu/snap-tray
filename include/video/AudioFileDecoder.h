#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <optional>
#include "recording/AudioClipTrack.h"

namespace SnapTray {
// Implemented by each platform's native media backend. The sink receives
// bounded interleaved PCM16 stereo at 48 kHz and its media sample position
// (nullopt when unavailable); false cancels. Positions preserve source gaps.
using AudioPcmSink = std::function<bool(const QByteArray&, std::optional<qint64>)>;
bool decodeAudioFile(const QString& path, const AudioPcmSink& sink, QString& error);
// Prepare the legacy mixed track for the shared preview time stretcher.
bool cacheRecordingAudio(const QString& videoPath, qint64 audioStartMs, QString& error,
                         const std::function<bool()>& cancelled = {});
// Transactional append: failed/oversize imports leave no usable empty clip.
std::optional<AudioClip> importAudioClip(const QString& input, const QString& videoPath, QString& error,
                                         const std::function<bool()>& cancelled = {});
}
