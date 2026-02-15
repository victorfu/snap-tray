#include "cli/commands/RegionCommand.h"

#include "cli/CaptureOutputHelper.h"
#include "utils/CoordinateHelper.h"

#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

namespace SnapTray {
namespace CLI {

QString RegionCommand::name() const { return "region"; }

QString RegionCommand::description() const { return "Capture specified region"; }

void RegionCommand::setupOptions(QCommandLineParser& parser)
{
    parser.addOption({{"r", "region"}, "Region definition in logical pixels (x,y,width,height)", "rect"});
    parser.addOption({{"n", "screen"}, "Screen number (default: primary)", "num", "0"});
    parser.addOption({{"d", "delay"}, "Delay in milliseconds before capture", "ms", "0"});
    parser.addOption({{"p", "path"}, "Save directory", "dir"});
    parser.addOption({{"o", "output"}, "Output file path", "file"});
    parser.addOption({{"c", "clipboard"}, "Copy to clipboard"});
    parser.addOption({"raw", "Output raw PNG to stdout"});
}

CLIResult RegionCommand::execute(const QCommandLineParser& parser)
{
    // Region is required
    if (!parser.isSet("region")) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            "Region required. Use --region x,y,width,height");
    }

    // Parse region
    QString regionStr = parser.value("region");
    QStringList parts = regionStr.split(',');
    if (parts.size() != 4) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            "Invalid region format. Use: x,y,width,height");
    }

    bool ok;
    int x = parts[0].toInt(&ok);
    if (!ok) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid x coordinate");
    }
    int y = parts[1].toInt(&ok);
    if (!ok) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid y coordinate");
    }
    int width = parts[2].toInt(&ok);
    if (!ok || width <= 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid width");
    }
    int height = parts[3].toInt(&ok);
    if (!ok || height <= 0) {
        return CLIResult::error(CLIResult::Code::InvalidArguments, "Invalid height");
    }

    QRect region(x, y, width, height);

    // Parse other arguments
    const QString screenValue = parser.value("screen");
    bool screenOk = false;
    int screenNum = screenValue.toInt(&screenOk);
    if (!screenOk) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid screen number: %1").arg(screenValue));
    }

    const QString delayValue = parser.value("delay");
    bool delayOk = false;
    int delay = delayValue.toInt(&delayOk);
    if (!delayOk) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid delay value: %1").arg(delayValue));
    }
    QString savePath = parser.value("path");
    QString outputFile = parser.value("output");
    bool toClipboard = parser.isSet("clipboard");
    bool toRaw = parser.isSet("raw");

    QList<QScreen*> screens = QGuiApplication::screens();
    if (screenNum < 0 || screenNum >= screens.size()) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Invalid screen number: %1 (available: 0-%2)")
                .arg(screenNum)
                .arg(screens.size() - 1));
    }

    // Delay before capture to preserve CLI behavior.
    applyOptionalDelay(delay);

    QScreen* screen = screens.at(screenNum);

    // Capture full screen then crop
    QPixmap fullScreenshot = screen->grabWindow(0);
    if (fullScreenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to capture screen");
    }

    // Region option uses logical coordinates. Validate against logical screen bounds.
    const qreal dpr = fullScreenshot.devicePixelRatio() > 0.0 ? fullScreenshot.devicePixelRatio() : 1.0;
    const QSize logicalScreenSize = CoordinateHelper::toLogical(fullScreenshot.size(), dpr);
    const QRect logicalScreenRect(QPoint(0, 0), logicalScreenSize);
    if (!logicalScreenRect.contains(region)) {
        return CLIResult::error(
            CLIResult::Code::InvalidArguments,
            QString("Region (%1,%2,%3,%4) exceeds logical screen bounds (%5x%6)")
                .arg(region.x())
                .arg(region.y())
                .arg(region.width())
                .arg(region.height())
                .arg(logicalScreenRect.width())
                .arg(logicalScreenRect.height()));
    }

    // Crop using edge-aligned physical coordinates derived from logical region.
    const QRect physicalRegion = CoordinateHelper::toPhysicalCoveringRect(region, dpr);
    const QRect clampedPhysicalRegion = physicalRegion.intersected(fullScreenshot.rect());
    if (clampedPhysicalRegion.isEmpty()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to crop region");
    }
    QPixmap screenshot = fullScreenshot.copy(clampedPhysicalRegion);
    screenshot.setDevicePixelRatio(dpr);

    if (screenshot.isNull()) {
        return CLIResult::error(CLIResult::Code::GeneralError, "Failed to crop region");
    }

    CaptureOutputOptions options;
    options.savePath = savePath;
    options.outputFile = outputFile;
    options.toClipboard = toClipboard;
    options.toRaw = toRaw;

    CaptureMetadata metadata;
    metadata.sourceScreen = screen;

    return emitCaptureOutput(screenshot, options, metadata);
}

} // namespace CLI
} // namespace SnapTray
