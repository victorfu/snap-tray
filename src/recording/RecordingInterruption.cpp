#include "recording/RecordingInterruption.h"

#include <QCoreApplication>

namespace SnapTray {

namespace {

constexpr auto kContext = "RecordingInterruption";

struct ReasonText {
    RecordingStopReason reason;
    const char* name;
    const char* cause;  // translated; empty for a normal stop
};

constexpr ReasonText kReasons[] = {
    {RecordingStopReason::User, "user", ""},
    {RecordingStopReason::SystemControl, "system-control",
     QT_TRANSLATE_NOOP("RecordingInterruption", "Screen recording was stopped from the system's controls.")},
    {RecordingStopReason::ScreenDisconnected, "screen-disconnected",
     QT_TRANSLATE_NOOP("RecordingInterruption", "The recorded screen was disconnected.")},
    {RecordingStopReason::ScreenChanged, "screen-changed",
     QT_TRANSLATE_NOOP("RecordingInterruption",
                       "The recorded screen's resolution, scaling or orientation changed.")},
    {RecordingStopReason::CaptureFailed, "capture-failed",
     QT_TRANSLATE_NOOP("RecordingInterruption", "Screen capture stopped working.")},
    {RecordingStopReason::DiskFull, "disk-full",
     QT_TRANSLATE_NOOP("RecordingInterruption", "The disk is almost full.")},
};

const ReasonText& entryFor(RecordingStopReason reason)
{
    for (const auto& entry : kReasons) {
        if (entry.reason == reason) return entry;
    }
    return kReasons[0];
}

} // namespace

QString recordingStopCause(RecordingStopReason reason)
{
    const ReasonText& entry = entryFor(reason);
    return *entry.cause ? QCoreApplication::translate(kContext, entry.cause) : QString();
}

QString recordingStopMessage(RecordingStopReason reason, const QString& detail)
{
    const QString cause = recordingStopCause(reason);
    if (cause.isEmpty()) {
        return {};
    }
    QString message = QCoreApplication::translate(kContext, "Recording stopped. %1 Everything recorded so far is kept.")
                          .arg(cause);
    if (!detail.isEmpty()) {
        message += QLatin1String(" (") + detail + QLatin1Char(')');
    }
    return message;
}

const char* recordingStopReasonName(RecordingStopReason reason)
{
    return entryFor(reason).name;
}

} // namespace SnapTray
