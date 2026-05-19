#include "update/InstallSourceDetector.h"

#include <QtGlobal>

InstallSource InstallSourceDetector::detect()
{
    if (s_detected) {
        return s_cachedSource;
    }

    if (isDevelopmentBuild()) {
        s_cachedSource = InstallSource::Development;
    } else if (qEnvironmentVariableIsSet("APPIMAGE")) {
        s_cachedSource = InstallSource::AppImage;
    } else {
        s_cachedSource = InstallSource::DirectDownload;
    }

    s_detected = true;
    return s_cachedSource;
}
