#include "utils/ScreenCaptureRegionUtils.h"

#include "utils/CoordinateHelper.h"

namespace SnapTray::ScreenCaptureRegionUtils {

LogicalRegionCropResult cropLogicalRegionFromScreenshot(const QPixmap& screenshot,
                                                        const QRect& logicalRegion)
{
    LogicalRegionCropResult result;
    if (screenshot.isNull()) {
        result.error = QStringLiteral("Failed to capture screen");
        return result;
    }

    result.devicePixelRatio =
        screenshot.devicePixelRatio() > 0.0 ? screenshot.devicePixelRatio() : 1.0;
    result.logicalScreenSize =
        CoordinateHelper::toLogical(screenshot.size(), result.devicePixelRatio);

    const QRect logicalScreenRect(QPoint(0, 0), result.logicalScreenSize);
    if (!logicalScreenRect.contains(logicalRegion)) {
        result.error = QStringLiteral(
            "Region (%1,%2,%3,%4) exceeds logical screen bounds (%5x%6)")
                           .arg(logicalRegion.x())
                           .arg(logicalRegion.y())
                           .arg(logicalRegion.width())
                           .arg(logicalRegion.height())
                           .arg(logicalScreenRect.width())
                           .arg(logicalScreenRect.height());
        return result;
    }

    const QRect physicalRegion =
        CoordinateHelper::toPhysicalCoveringRect(logicalRegion, result.devicePixelRatio);
    const QRect clampedPhysicalRegion = physicalRegion.intersected(screenshot.rect());
    if (clampedPhysicalRegion.isEmpty()) {
        result.error = QStringLiteral("Failed to crop region");
        return result;
    }

    result.pixmap = screenshot.copy(clampedPhysicalRegion);
    if (result.pixmap.isNull()) {
        result.error = QStringLiteral("Failed to crop region");
        return result;
    }

    result.pixmap.setDevicePixelRatio(result.devicePixelRatio);
    return result;
}

} // namespace SnapTray::ScreenCaptureRegionUtils
