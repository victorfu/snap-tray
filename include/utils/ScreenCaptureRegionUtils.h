#pragma once

#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

namespace SnapTray::ScreenCaptureRegionUtils {

struct LogicalRegionCropResult
{
    QPixmap pixmap;
    QSize logicalScreenSize;
    qreal devicePixelRatio = 1.0;
    QString error;

    bool isValid() const
    {
        return error.isEmpty() && !pixmap.isNull();
    }
};

LogicalRegionCropResult cropLogicalRegionFromScreenshot(const QPixmap& screenshot,
                                                        const QRect& logicalRegion);

} // namespace SnapTray::ScreenCaptureRegionUtils
