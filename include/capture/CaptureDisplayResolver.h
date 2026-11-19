#ifndef CAPTUREDISPLAYRESOLVER_H
#define CAPTUREDISPLAYRESOLVER_H

#include <QList>
#include <QRect>
#include <QtGlobal>

namespace SnapTray {

// A native display as the platform reports it: its identifier (a
// CGDirectDisplayID on macOS) and its bounds in global logical coordinates.
struct CaptureDisplayCandidate
{
    quint32 id = 0;
    QRect bounds;
};

enum class CaptureDisplayResolution {
    Resolved,
    IdentityUnknown,  // The selected screen never mapped to a native display.
    NotShareable,     // The display is no longer offered for capture.
    GeometryChanged,  // The display moved or was resized after selection.
};

// Identifies the native display behind a selected screen on the GUI thread.
// Only an unambiguous geometry match counts; 0 means the identity is unknown.
inline quint32 findCaptureDisplayId(const QRect& screenGeometry,
                                    const QList<CaptureDisplayCandidate>& displays)
{
    quint32 match = 0;
    for (const CaptureDisplayCandidate& display : displays) {
        if (display.id == 0 || display.bounds != screenGeometry)
            continue;
        if (match != 0 && match != display.id)
            return 0;
        match = display.id;
    }
    return match;
}

// Confirms that the display chosen at selection time is still the one to
// capture. It never substitutes another display: a capture source that cannot
// be confirmed is an error, not a reason to record a different screen.
inline CaptureDisplayResolution resolveCaptureDisplay(
    quint32 requestedId,
    const QRect& requestedGeometry,
    const QList<CaptureDisplayCandidate>& shareableDisplays)
{
    if (requestedId == 0)
        return CaptureDisplayResolution::IdentityUnknown;
    for (const CaptureDisplayCandidate& display : shareableDisplays) {
        if (display.id != requestedId)
            continue;
        return display.bounds == requestedGeometry
            ? CaptureDisplayResolution::Resolved
            : CaptureDisplayResolution::GeometryChanged;
    }
    return CaptureDisplayResolution::NotShareable;
}

#ifdef Q_OS_MACOS
// Logical global bounds of a CGDirectDisplayID, in QScreen::geometry() space.
QRect captureDisplayBounds(quint32 displayId);
// Active displays that can be selected, without software-mirror secondaries.
QList<CaptureDisplayCandidate> activeCaptureDisplays();
#endif

} // namespace SnapTray

#endif // CAPTUREDISPLAYRESOLVER_H
