#pragma once
#include <QMetaType>
#include <array>
namespace SnapTray {
struct AudioSourceDiagnostics {
    qint64 gapCount = 0, gapFrames = 0, maxGapFrames = 0;
    qint64 trimCount = 0, trimFrames = 0;
    qint64 timestampErrors = 0, timestampRealignments = 0;
};
struct AudioCaptureDiagnostics {
    std::array<AudioSourceDiagnostics, 2> sources{}; // microphone, system
};
}
Q_DECLARE_METATYPE(SnapTray::AudioCaptureDiagnostics)
