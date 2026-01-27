#include "update/InstallSourceDetector.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>

#ifdef Q_OS_MAC
#import <Foundation/Foundation.h>

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

    // Check for Mac App Store receipt
    // Apps from the Mac App Store contain a receipt file at _MASReceipt/receipt
    NSBundle* mainBundle = [NSBundle mainBundle];
    if (mainBundle) {
        NSURL* receiptURL = [mainBundle appStoreReceiptURL];
        if (receiptURL) {
            NSString* receiptPath = [receiptURL path];
            if ([[NSFileManager defaultManager] fileExistsAtPath:receiptPath]) {
                qDebug() << "InstallSourceDetector: Mac App Store receipt found";
                s_cachedSource = InstallSource::MacAppStore;
                return s_cachedSource;
            }
        }
    }

    // Check for Homebrew installation
    QString appPath = QCoreApplication::applicationDirPath();

    // Homebrew on Apple Silicon
    if (appPath.startsWith("/opt/homebrew")) {
        qDebug() << "InstallSourceDetector: Homebrew (Apple Silicon) detected";
        s_cachedSource = InstallSource::Homebrew;
        return s_cachedSource;
    }

    // Homebrew on Intel
    if (appPath.startsWith("/usr/local/Cellar")) {
        qDebug() << "InstallSourceDetector: Homebrew (Intel) detected";
        s_cachedSource = InstallSource::Homebrew;
        return s_cachedSource;
    }

    // Also check for Homebrew cask installations
    if (appPath.contains("/Homebrew/Caskroom")) {
        qDebug() << "InstallSourceDetector: Homebrew Cask detected";
        s_cachedSource = InstallSource::Homebrew;
        return s_cachedSource;
    }

    // Default to direct download
    qDebug() << "InstallSourceDetector: Direct download detected";
    s_cachedSource = InstallSource::DirectDownload;
    return s_cachedSource;
}

#endif // Q_OS_MAC
