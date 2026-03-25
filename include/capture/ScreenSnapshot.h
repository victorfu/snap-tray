#ifndef SCREENSNAPSHOT_H
#define SCREENSNAPSHOT_H

#include <QPixmap>

class QScreen;

namespace snaptray::capture {

QPixmap captureScreenSnapshot(QScreen* screen);

} // namespace snaptray::capture

#endif // SCREENSNAPSHOT_H
