#ifndef RECORDINGINTERRUPTION_H
#define RECORDINGINTERRUPTION_H

#include <QString>

namespace SnapTray {

// Why a recording ended other than the user pressing Stop. Every reason
// keeps what was recorded: the recording is finished normally (preview or
// save). Only the user's explicit Discard deletes it.
enum class RecordingStopReason {
    User,                // Stop button, tray, hotkey
    SystemControl,       // the OS's own screen-sharing control stopped capture
    ScreenDisconnected,  // the recorded screen went away
    ScreenChanged,       // its resolution, scale or orientation changed
    CaptureFailed,       // capture permission revoked or the backend failed
    DiskFull,            // safe stop before the disk runs out
};

// What the user is told when a recording is interrupted while recording.
// `detail` is an optional technical cause shown after the explanation.
QString recordingStopMessage(RecordingStopReason reason, const QString& detail = QString());

// The translated cause alone ("The recorded screen was disconnected.");
// empty for a normal stop.
QString recordingStopCause(RecordingStopReason reason);

// Short reason, for logs and diagnostics.
const char* recordingStopReasonName(RecordingStopReason reason);

} // namespace SnapTray

#include <QMetaType>
Q_DECLARE_METATYPE(SnapTray::RecordingStopReason)

#endif // RECORDINGINTERRUPTION_H
