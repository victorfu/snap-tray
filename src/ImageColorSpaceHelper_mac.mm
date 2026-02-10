#include "ImageColorSpaceHelper.h"

#include <QColorSpace>
#include <QImage>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace {

CGColorSpaceRef createSRGBColorSpace()
{
    CGColorSpaceRef sRGB = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    if (sRGB) {
        return sRGB;
    }
    return CGColorSpaceCreateDeviceRGB();
}

CGColorSpaceRef createSourceColorSpace(const QImage& image)
{
    const QByteArray iccProfile = image.colorSpace().iccProfile();
    if (!iccProfile.isEmpty()) {
        CFDataRef iccData = CFDataCreate(
            nullptr,
            reinterpret_cast<const UInt8*>(iccProfile.constData()),
            static_cast<CFIndex>(iccProfile.size()));
        if (iccData) {
            CGColorSpaceRef iccColorSpace = CGColorSpaceCreateWithICCData(iccData);
            CFRelease(iccData);
            if (iccColorSpace) {
                return iccColorSpace;
            }
        }
    }

    return createSRGBColorSpace();
}

CGColorSpaceRef createTargetColorSpace()
{
    NSScreen* mainScreen = [NSScreen mainScreen];
    if (!mainScreen) {
        NSArray<NSScreen*>* screens = [NSScreen screens];
        if (screens.count > 0) {
            mainScreen = screens.firstObject;
        }
    }

    if (mainScreen) {
        NSColorSpace* screenColorSpace = [mainScreen colorSpace];
        if (screenColorSpace && screenColorSpace.CGColorSpace) {
            return CGColorSpaceRetain(screenColorSpace.CGColorSpace);
        }
    }

    return createSRGBColorSpace();
}

} // namespace

QImage convertImageForDisplay(const QImage& image)
{
    if (image.isNull()) {
        return image;
    }

    QImage source = image.convertToFormat(QImage::Format_RGBA8888);
    if (source.isNull()) {
        return image;
    }

    CGColorSpaceRef sourceColorSpace = createSourceColorSpace(source);
    if (!sourceColorSpace) {
        return image;
    }

    CGDataProviderRef sourceProvider = CGDataProviderCreateWithData(
        nullptr,
        source.constBits(),
        static_cast<size_t>(source.sizeInBytes()),
        nullptr);
    if (!sourceProvider) {
        CGColorSpaceRelease(sourceColorSpace);
        return image;
    }

    CGImageRef sourceImage = CGImageCreate(
        static_cast<size_t>(source.width()),
        static_cast<size_t>(source.height()),
        8,
        32,
        static_cast<size_t>(source.bytesPerLine()),
        sourceColorSpace,
        kCGImageAlphaLast | kCGBitmapByteOrder32Big,
        sourceProvider,
        nullptr,
        false,
        kCGRenderingIntentDefault);
    CGDataProviderRelease(sourceProvider);
    CGColorSpaceRelease(sourceColorSpace);
    if (!sourceImage) {
        return image;
    }

    QImage target(source.size(), QImage::Format_RGBA8888_Premultiplied);
    if (target.isNull()) {
        CGImageRelease(sourceImage);
        return image;
    }

    CGColorSpaceRef targetColorSpace = createTargetColorSpace();
    if (!targetColorSpace) {
        CGImageRelease(sourceImage);
        return image;
    }

    CGContextRef targetContext = CGBitmapContextCreate(
        target.bits(),
        static_cast<size_t>(target.width()),
        static_cast<size_t>(target.height()),
        8,
        static_cast<size_t>(target.bytesPerLine()),
        targetColorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(targetColorSpace);
    if (!targetContext) {
        CGImageRelease(sourceImage);
        return image;
    }

    CGContextSetBlendMode(targetContext, kCGBlendModeCopy);
    CGContextDrawImage(targetContext,
                       CGRectMake(0.0, 0.0,
                                  static_cast<CGFloat>(target.width()),
                                  static_cast<CGFloat>(target.height())),
                       sourceImage);

    CGContextRelease(targetContext);
    CGImageRelease(sourceImage);

    // Avoid any secondary conversion in Qt paint paths.
    target.setColorSpace(QColorSpace());
    return target;
}
