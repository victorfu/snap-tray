#include "PlatformFeatures.h"
#include "OCRManager.h"
#include "WindowDetector.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QProcess>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

// ScreenCaptureKit availability check
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120300
#define HAS_SCREENCAPTUREKIT 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#endif

namespace {

QString escapeForSingleQuotedShellLiteral(const QString& value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("'\"'\"'"));
    return escaped;
}

} // namespace

PlatformFeatures& PlatformFeatures::instance()
{
    static PlatformFeatures instance;
    return instance;
}

PlatformFeatures::PlatformFeatures()
    : m_ocrAvailable(OCRManager::isAvailable())
    , m_windowDetectionAvailable(true)
{
}

PlatformFeatures::~PlatformFeatures() = default;

bool PlatformFeatures::isOCRAvailable() const
{
    return m_ocrAvailable;
}

OCRManager* PlatformFeatures::createOCRManager(QObject* parent) const
{
    if (!m_ocrAvailable) {
        return nullptr;
    }
    return new OCRManager(parent);
}

WindowDetector* PlatformFeatures::createWindowDetector(QObject* parent) const
{
    if (!m_windowDetectionAvailable) {
        return nullptr;
    }
    return new WindowDetector(parent);
}

QIcon PlatformFeatures::createTrayIcon() const
{
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Capsule background
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);

    // Lightning bolt cutout
    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);
    lightningPath.lineTo(8, 17);
    lightningPath.lineTo(15, 17);
    lightningPath.lineTo(13, 29);
    lightningPath.lineTo(24, 14);
    lightningPath.lineTo(17, 14);
    lightningPath.closeSubpath();

    QPainterPath finalPath = bgPath.subtracted(lightningPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(finalPath);

    return QIcon(pixmap);
}

bool PlatformFeatures::copyImageToClipboard(const QImage& image) const
{
    if (image.isNull()) {
        return false;
    }

    // Convert QImage to PNG data
    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return false;
    }

    // Use NSPasteboard directly to ensure data persists after app exit
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        NSData* data = [NSData dataWithBytes:pngData.constData() length:pngData.size()];

        // Write PNG data with public.png UTI
        BOOL success = [pasteboard setData:data forType:NSPasteboardTypePNG];

        return success == YES;
    }
}

QString PlatformFeatures::getAppExecutablePath() const
{
    // Return app bundle path (not executable path)
    // /Applications/SnapTray.app/Contents/MacOS/SnapTray -> /Applications/SnapTray.app
    QString appFilePath = QCoreApplication::applicationFilePath();
    QDir dir(appFilePath);
    dir.cdUp(); // MacOS
    dir.cdUp(); // Contents
    dir.cdUp(); // SnapTray.app
    return dir.absolutePath();
}

bool PlatformFeatures::isCLIInstalled() const
{
    QFileInfo cliFile("/usr/local/bin/snaptray");
    if (!cliFile.exists()) {
        return false;
    }

    // Read the script and check if it references our app
    QFile file("/usr/local/bin/snaptray");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QString content = QString::fromUtf8(file.readAll());
    return content.contains(getAppExecutablePath());
}

bool PlatformFeatures::installCLI() const
{
    QString appPath = getAppExecutablePath();
    const QString escapedAppPath = escapeForSingleQuotedShellLiteral(appPath);

    // Create shell script content that sets up Qt environment
    // Using printf for proper newline handling in shell
    QString script = QString(
        "do shell script \"mkdir -p /usr/local/bin && "
        "printf '#!/bin/bash\\n"
        "APP_PATH=\\\"%1\\\"\\n"
        "export QT_PLUGIN_PATH=\\\"$APP_PATH/Contents/PlugIns\\\"\\n"
        "exec \\\"$APP_PATH/Contents/MacOS/SnapTray\\\" \\\"$@\\\"\\n' > /usr/local/bin/snaptray && "
        "chmod +x /usr/local/bin/snaptray\" "
        "with administrator privileges"
    ).arg(escapedAppPath);

    QProcess process;
    process.start("osascript", {"-e", script});
    process.waitForFinished(-1);
    return process.exitCode() == 0;
}

bool PlatformFeatures::uninstallCLI() const
{
    QString script = "do shell script \"rm -f /usr/local/bin/snaptray\" with administrator privileges";
    QProcess process;
    process.start("osascript", {"-e", script});
    process.waitForFinished(-1);
    return process.exitCode() == 0;
}

// macOS permission management

bool PlatformFeatures::hasScreenRecordingPermission()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        __block BOOL hasPermission = NO;
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

        [SCShareableContent getShareableContentWithCompletionHandler:^(
            SCShareableContent *content, NSError *error) {
            hasPermission = (content != nil && error == nil);
            dispatch_semaphore_signal(semaphore);
        }];

        dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        return hasPermission;
    }
#endif
    // For older macOS versions, assume permission granted
    return true;
}

bool PlatformFeatures::hasAccessibilityPermission()
{
    NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @NO};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void PlatformFeatures::openScreenRecordingSettings()
{
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

void PlatformFeatures::openAccessibilitySettings()
{
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }
}
