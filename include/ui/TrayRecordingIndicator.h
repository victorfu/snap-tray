#ifndef TRAYRECORDINGINDICATOR_H
#define TRAYRECORDINGINDICATOR_H

#include <QPixmap>
#include <QString>

namespace SnapTray {

// Recording status shown on the tray / menu bar icon. Preparing and
// Countdown share the "starting" badge; the tooltip text tells them apart.
enum class TrayRecordingStatus {
    Idle,
    Preparing,
    Countdown,
    Recording,
    Paused,
    Finalizing,
};

// Returns `base` with a corner badge for `status`. Each status uses a distinct
// shape (ring, solid dot, pause bars, ellipsis) so it does not rely on color
// alone. Idle returns `base` unchanged. The badge is drawn in the pixmap's
// device-independent coordinates, so high-DPI tray pixmaps stay sharp.
QPixmap composeTrayRecordingPixmap(const QPixmap& base, TrayRecordingStatus status);

// Formats a recording duration as m:ss, or h:mm:ss from one hour.
// Negative values clamp to zero.
QString formatTrayRecordingElapsed(qint64 elapsedMs);

} // namespace SnapTray

#endif // TRAYRECORDINGINDICATOR_H
