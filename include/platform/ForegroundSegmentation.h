#ifndef FOREGROUNDSEGMENTATION_H
#define FOREGROUNDSEGMENTATION_H

#include <QImage>

#include "pinwindow/ForegroundCutout.h"

namespace SnapTray {

// The platform's foreground segmenter (#363). macOS 14+ uses Vision's
// on-device VNGenerateForegroundInstanceMaskRequest over every instance it
// finds. Other platforms have no validated backend and report Unsupported;
// nothing is downloaded or uploaded to make up for it.
//
// Blocking and safe to call on any thread; never call it on the GUI thread.
ForegroundCutout::MaskResult generateForegroundMask(const QImage& image);

} // namespace SnapTray

#endif // FOREGROUNDSEGMENTATION_H
