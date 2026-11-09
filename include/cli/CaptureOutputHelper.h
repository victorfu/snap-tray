#ifndef CAPTURE_OUTPUT_HELPER_H
#define CAPTURE_OUTPUT_HELPER_H

#include "cli/CLIResult.h"

#include <QString>

class QPixmap;
class QScreen;

namespace SnapTray {
namespace CLI {

struct CaptureOutputOptions
{
    QString savePath;
    QString outputFile;
    bool toClipboard = false;
    bool toRaw = false;
    // Keep the alpha channel in file and raw PNG output. GUI captures can have
    // transparent rounded corners; screen grabs stay opaque RGB32 by default.
    bool preserveAlpha = false;
};

struct CaptureMetadata
{
    QScreen* sourceScreen = nullptr;
    QString captureType;
};

void applyOptionalDelay(int delayMs);
bool writeRawOutputToStdout(const QByteArray& data);
CLIResult emitCaptureOutput(
    const QPixmap& screenshot,
    const CaptureOutputOptions& options,
    const CaptureMetadata& metadata = {});

} // namespace CLI
} // namespace SnapTray

#endif // CAPTURE_OUTPUT_HELPER_H
