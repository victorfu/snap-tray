#include "update/InstallSourceDetector.h"

// Static member initialization
InstallSource InstallSourceDetector::s_cachedSource = InstallSource::Unknown;
bool InstallSourceDetector::s_detected = false;

bool InstallSourceDetector::isStoreInstall()
{
    InstallSource source = detect();
    return source == InstallSource::MicrosoftStore ||
           source == InstallSource::MacAppStore;
}

QString InstallSourceDetector::getStoreName()
{
    InstallSource source = detect();
    switch (source) {
    case InstallSource::MicrosoftStore:
        return QStringLiteral("Microsoft Store");
    case InstallSource::MacAppStore:
        return QStringLiteral("Mac App Store");
    default:
        return QString();
    }
}

QString InstallSourceDetector::getSourceDisplayName(InstallSource source)
{
    switch (source) {
    case InstallSource::MicrosoftStore:
        return QStringLiteral("Microsoft Store");
    case InstallSource::MacAppStore:
        return QStringLiteral("Mac App Store");
    case InstallSource::DirectDownload:
        return QStringLiteral("Direct Download");
    case InstallSource::Homebrew:
        return QStringLiteral("Homebrew");
    case InstallSource::Development:
        return QStringLiteral("Development Build");
    case InstallSource::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

bool InstallSourceDetector::isDevelopmentBuild()
{
#ifdef QT_DEBUG
    return true;
#else
    return false;
#endif
}

// Platform-specific detect() implementation is in:
// - InstallSourceDetector_win.cpp (Windows)
// - InstallSourceDetector_mac.mm (macOS)
