#include "platform/ForegroundSegmentation.h"

namespace SnapTray {

ForegroundCutout::MaskResult generateForegroundMask(const QImage&)
{
    ForegroundCutout::MaskResult result;
    result.status = ForegroundCutout::Status::Unsupported;
    return result;
}

} // namespace SnapTray
