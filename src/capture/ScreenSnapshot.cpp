#include "capture/ScreenSnapshot.h"

#include <QByteArray>
#include <QColorSpace>
#include <QImage>
#include <QScreen>
#include <optional>

#ifdef Q_OS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace snaptray::capture {

namespace {

#ifdef Q_OS_MACOS
QColorSpace qtColorSpaceFromCGColorSpace(CGColorSpaceRef colorSpace)
{
    if (!colorSpace) {
        return {};
    }

    CFDataRef iccData = CGColorSpaceCopyICCData(colorSpace);
    if (!iccData) {
        return {};
    }

    const QByteArray iccProfile(
        reinterpret_cast<const char*>(CFDataGetBytePtr(iccData)),
        static_cast<int>(CFDataGetLength(iccData)));
    CFRelease(iccData);

    return QColorSpace::fromIccProfile(iccProfile);
}

CGColorSpaceRef copySnapshotColorSpace(CGImageRef sourceImage,
                                       std::optional<CGDirectDisplayID> displayId,
                                       QColorSpace* qtColorSpace)
{
    if (CGColorSpaceRef imageColorSpace = CGImageGetColorSpace(sourceImage)) {
        if (qtColorSpace) {
            *qtColorSpace = qtColorSpaceFromCGColorSpace(imageColorSpace);
            if (!qtColorSpace->isValid() && displayId.has_value()) {
                if (CGColorSpaceRef displayColorSpace = CGDisplayCopyColorSpace(*displayId)) {
                    *qtColorSpace = qtColorSpaceFromCGColorSpace(displayColorSpace);
                    CGColorSpaceRelease(displayColorSpace);
                }
            }
        }
        return CGColorSpaceRetain(imageColorSpace);
    }

    if (displayId.has_value()) {
        if (CGColorSpaceRef displayColorSpace = CGDisplayCopyColorSpace(*displayId)) {
            if (qtColorSpace) {
                *qtColorSpace = qtColorSpaceFromCGColorSpace(displayColorSpace);
            }
            return displayColorSpace;
        }
    }

    if (qtColorSpace) {
        const QColorSpace sRgb(QColorSpace::SRgb);
        *qtColorSpace = sRgb.isValid() ? sRgb : QColorSpace();
    }

    CGColorSpaceRef sRgbColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (sRgbColorSpace) {
        return sRgbColorSpace;
    }
    return CGColorSpaceCreateDeviceRGB();
}

QImage createQImageFromCGImage(CGImageRef sourceImage, std::optional<CGDirectDisplayID> displayId)
{
    if (!sourceImage) {
        return {};
    }

    const size_t width = CGImageGetWidth(sourceImage);
    const size_t height = CGImageGetHeight(sourceImage);
    if (width == 0 || height == 0) {
        return {};
    }

    QImage target(static_cast<int>(width), static_cast<int>(height),
                  QImage::Format_RGBA8888_Premultiplied);
    if (target.isNull()) {
        return {};
    }

    QColorSpace qtColorSpace;
    CGColorSpaceRef colorSpace = copySnapshotColorSpace(sourceImage, displayId, &qtColorSpace);
    if (!colorSpace) {
        return {};
    }

    CGContextRef context = CGBitmapContextCreate(
        target.bits(),
        width,
        height,
        8,
        static_cast<size_t>(target.bytesPerLine()),
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(colorSpace);

    if (!context) {
        return {};
    }

    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context,
                       CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       sourceImage);
    CGContextRelease(context);

    if (qtColorSpace.isValid()) {
        target.setColorSpace(qtColorSpace);
    }
    return target;
}

std::optional<CGDirectDisplayID> displayIdForScreen(QScreen* screen)
{
    if (!screen) {
        return std::nullopt;
    }

    uint32_t displayCount = 0;
    CGDirectDisplayID displays[16];
    if (CGGetActiveDisplayList(16, displays, &displayCount) != kCGErrorSuccess) {
        return std::nullopt;
    }

    const QRect targetGeometry = screen->geometry();
    for (uint32_t i = 0; i < displayCount; ++i) {
        const CGRect bounds = CGDisplayBounds(displays[i]);
        if (static_cast<int>(bounds.origin.x) == targetGeometry.x() &&
            static_cast<int>(bounds.origin.y) == targetGeometry.y() &&
            static_cast<int>(bounds.size.width) == targetGeometry.width() &&
            static_cast<int>(bounds.size.height) == targetGeometry.height()) {
            return displays[i];
        }
    }

    return std::nullopt;
}

QPixmap captureScreenViaNativeDisplay(QScreen* screen)
{
    const auto displayId = displayIdForScreen(screen);
    if (!displayId.has_value()) {
        return {};
    }

    CGImageRef displayImage = CGDisplayCreateImage(*displayId);
    if (!displayImage) {
        return {};
    }

    const QImage image = createQImageFromCGImage(displayImage, displayId);
    CGImageRelease(displayImage);
    if (image.isNull()) {
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image, Qt::NoOpaqueDetection);
    if (pixmap.isNull()) {
        return {};
    }

    const qreal dpr = screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}
#endif

} // namespace

QPixmap captureScreenSnapshot(QScreen* screen)
{
    if (!screen) {
        return {};
    }

#ifdef Q_OS_MACOS
    QPixmap nativePixmap = captureScreenViaNativeDisplay(screen);
    if (!nativePixmap.isNull()) {
        return nativePixmap;
    }
#endif

    return screen->grabWindow(0);
}

} // namespace snaptray::capture
