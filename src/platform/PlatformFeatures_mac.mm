#include "PlatformFeatures.h"
#include "platform/ClipboardImageFile.h"
#include "platform/ClipboardImageFile_mac.h"
#include "platform/MacCLIInstaller.h"
#include "OCRManager.h"
#include "WindowDetector.h"
#include <QBuffer>
#include <QClipboard>
#include <QDebug>
#include <QCoreApplication>
#include <QGuiApplication>
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

QByteArray encodePngImage(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }

    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) {
        return {};
    }

    return pngData;
}

bool writePngDataToGeneralPasteboard(const QByteArray& pngData)
{
    if (pngData.isEmpty()) {
        return false;
    }

    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        NSData* data = [NSData dataWithBytes:pngData.constData() length:pngData.size()];
        return [pasteboard setData:data forType:NSPasteboardTypePNG] == YES;
    }
}

bool writePngImageToGeneralPasteboard(const QImage& image)
{
    return writePngDataToGeneralPasteboard(encodePngImage(image));
}

} // namespace

PlatformFeatures& PlatformFeatures::instance()
{
    static PlatformFeatures instance;
    return instance;
}

PlatformFeatures::PlatformFeatures()
    : m_capabilities(SnapTray::currentPlatformCapabilities())
    , m_ocrAvailable(m_capabilities.supportsOCR && OCRManager::isAvailable())
    , m_windowDetectionAvailable(m_capabilities.supportsWindowDetection)
{
}

PlatformFeatures::~PlatformFeatures() = default;

const SnapTray::PlatformCapabilities& PlatformFeatures::capabilities() const
{
    return m_capabilities;
}

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

QPixmap PlatformFeatures::createTrayPixmap() const
{
    // Render the 32pt-grid design into a narrower canvas so the menu bar item
    // (and thus the system highlight pill) matches widths like Google Drive.
    const int logicalSize = 19;
    const qreal dpr = 2.0;
    QPixmap pixmap(static_cast<int>(logicalSize * dpr), static_cast<int>(logicalSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(logicalSize / 32.0, logicalSize / 32.0);

    // Capsule background
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, 32, 32, 16, 16);

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

    return pixmap;
}

bool PlatformFeatures::copyImageOnlyPersistently(const QImage& image) const
{
    return writePngImageToGeneralPasteboard(image);
}

bool PlatformFeatures::clipboardIsDefinitelyEmpty()
{
    NSArray* types = [NSPasteboard generalPasteboard].types;
    return types != nil && types.count == 0;
}

quint64 PlatformFeatures::nativeClipboardRevision()
{
    return [NSPasteboard generalPasteboard].changeCount;
}

PlatformFeatures::ClipboardCopyResult PlatformFeatures::publishClipboardImageFile(
    const QImage& image, SnapTray::ClipboardImageFile& file, bool includePixels, bool persistent) const
{
    Q_UNUSED(persistent);
    Q_UNUSED(image);
    return SnapTray::writeClipboardImageFile([NSPasteboard generalPasteboard], file, includePixels);
}

PlatformFeatures::ClipboardCopyResult SnapTray::writeClipboardImageFile(
    NSPasteboard* board, ClipboardImageFile& file, bool includePixels)
{
    using Result = PlatformFeatures::ClipboardCopyResult;
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:file.path.toNSString()];
        NSData* png = includePixels
            ? [NSData dataWithBytes:file.png.constData() length:file.png.size()] : nil;
        if (!url || !board || (includePixels && !png))
            return Result::Failed;
        id<NSPasteboardWriting> object = url;
        if (includePixels) {
            NSPasteboardItem* item = [[[NSPasteboardItem alloc] init] autorelease];
            // Carry the native NSURL representations alongside the rendered PNG.
            for (NSPasteboardType type in [url writableTypesForPasteboard:board]) {
                id value = [url pasteboardPropertyListForType:type];
                BOOL prepared;
                if ([value isKindOfClass:[NSString class]])
                    prepared = [item setString:value forType:type];
                else if ([value isKindOfClass:[NSData class]])
                    prepared = [item setData:value forType:type];
                else
                    prepared = value && [item setPropertyList:value forType:type];
                if (!prepared) {
                    qWarning() << "Cannot prepare clipboard file URL:" << file.path << QString::fromNSString(type);
                    return Result::Failed;
                }
            }
            if (![item.types containsObject:NSPasteboardTypeFileURL] || ![item setData:png forType:NSPasteboardTypePNG]) {
                qWarning() << "Cannot prepare clipboard PNG:" << file.path << file.png.size();
                return Result::Failed;
            }
            object = item;
        }
        [board clearContents];
        // From here the pasteboard server may have received the URL.
        file.offeredToClipboard = true;
        if (![board writeObjects:@[object]]) {
            qWarning() << "Pasteboard rejected screenshot after clearing:" << file.path;
            return Result::Incomplete;
        }
        return Result::Success;
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
    return SnapTray::isMacCLIWrapperInstalled(QStringLiteral("/usr/local/bin/snaptray"),
        QCoreApplication::applicationFilePath(), getAppExecutablePath());
}

bool PlatformFeatures::installCLI() const
{
    return SnapTray::installMacCLIWrapper(QStringLiteral("/usr/local/bin/snaptray"),
        QCoreApplication::applicationFilePath(), getAppExecutablePath(), [](const QString& command) {
            QProcess process;
            process.start(QStringLiteral("/usr/bin/osascript"),
                {QStringLiteral("-e"), QStringLiteral("do shell script ")
                    + SnapTray::quoteAppleScriptString(command)
                    + QStringLiteral(" with administrator privileges")});
            return process.waitForFinished(-1) && process.exitStatus() == QProcess::NormalExit
                && process.exitCode() == 0;
        });
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

void PlatformFeatures::activateApp()
{
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
