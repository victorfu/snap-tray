#include "update/InstallSourceDetector.h"
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <appmodel.h>

InstallSource InstallSourceDetector::detect()
{
    // Return cached result if already detected
    if (s_detected) {
        return s_cachedSource;
    }

    s_detected = true;

    // Check for debug build first
    if (isDevelopmentBuild()) {
        s_cachedSource = InstallSource::Development;
        qDebug() << "InstallSourceDetector: Development build detected";
        return s_cachedSource;
    }

    // Check if running as MSIX package (Microsoft Store or sideloaded)
    UINT32 length = 0;
    LONG result = GetCurrentPackageFullName(&length, nullptr);

    if (result == ERROR_INSUFFICIENT_BUFFER) {
        // We're running as a packaged app (MSIX)
        // Allocate buffer and get the package name
        wchar_t* packageFullName = new wchar_t[length];
        result = GetCurrentPackageFullName(&length, packageFullName);

        if (result == ERROR_SUCCESS) {
            QString packageName = QString::fromWCharArray(packageFullName);
            qDebug() << "InstallSourceDetector: MSIX package detected:" << packageName;
            delete[] packageFullName;
            s_cachedSource = InstallSource::MicrosoftStore;
            return s_cachedSource;
        }
        delete[] packageFullName;
    }

    // Fallback: Check if running from WindowsApps folder
    QString appPath = QCoreApplication::applicationDirPath();
    if (appPath.contains("WindowsApps", Qt::CaseInsensitive)) {
        qDebug() << "InstallSourceDetector: WindowsApps path detected";
        s_cachedSource = InstallSource::MicrosoftStore;
        return s_cachedSource;
    }

    // Not a packaged app - direct download
    qDebug() << "InstallSourceDetector: Direct download detected";
    s_cachedSource = InstallSource::DirectDownload;
    return s_cachedSource;
}

#endif // Q_OS_WIN
