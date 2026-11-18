#include "platform/ForegroundSegmentation.h"

#include <QDebug>

#include <algorithm>
#include <cmath>

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#if !__has_feature(objc_arc)
#error "ForegroundSegmentation_mac.mm requires Objective-C ARC"
#endif

namespace SnapTray {

namespace {

using ForegroundCutout::MaskResult;
using ForegroundCutout::Status;

// The returned image references `rgba`'s pixels; keep it alive while the
// CGImage is in use.
CGImageRef createCGImage(const QImage& rgba)
{
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) {
        return nullptr;
    }
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        nullptr, rgba.constBits(), static_cast<size_t>(rgba.sizeInBytes()), nullptr);
    if (!provider) {
        CGColorSpaceRelease(colorSpace);
        return nullptr;
    }
    CGImageRef image = CGImageCreate(
        rgba.width(), rgba.height(), 8, 32, rgba.bytesPerLine(), colorSpace,
        kCGBitmapByteOrderDefault | kCGImageAlphaLast, provider, nullptr, false,
        kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);
    return image;
}

QImage grayscaleMaskFromBuffer(CVPixelBufferRef buffer, const QSize& expectedSize)
{
    if (!buffer
        || CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_OneComponent32Float
        || static_cast<int>(CVPixelBufferGetWidth(buffer)) != expectedSize.width()
        || static_cast<int>(CVPixelBufferGetHeight(buffer)) != expectedSize.height()) {
        return QImage();
    }

    QImage mask(expectedSize, QImage::Format_Grayscale8);
    if (mask.isNull()) {
        return QImage();
    }
    if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        return QImage();
    }
    const auto* base = static_cast<const uchar*>(CVPixelBufferGetBaseAddress(buffer));
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < expectedSize.height(); ++y) {
        const auto* source = reinterpret_cast<const float*>(base + y * stride);
        uchar* target = mask.scanLine(y);
        for (int x = 0; x < expectedSize.width(); ++x) {
            const float coverage = std::clamp(source[x], 0.0f, 1.0f);
            target[x] = static_cast<uchar>(std::lround(coverage * 255.0f));
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return mask;
}

} // namespace

MaskResult generateForegroundMask(const QImage& image)
{
    MaskResult result;
    if (image.isNull()) {
        result.status = Status::Failed;
        return result;
    }

    if (@available(macOS 14.0, *)) {
        @autoreleasepool {
            // A private, non-premultiplied copy: Vision reads it, nothing else.
            const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
            CGImageRef cgImage = rgba.isNull() ? nullptr : createCGImage(rgba);
            if (!cgImage) {
                result.status = Status::Failed;
                return result;
            }

            VNImageRequestHandler* handler =
                [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
            VNGenerateForegroundInstanceMaskRequest* request =
                [[VNGenerateForegroundInstanceMaskRequest alloc] init];
            NSError* error = nil;
            const BOOL performed = [handler performRequests:@[request] error:&error];
            if (!performed) {
                qWarning() << "ForegroundSegmentation: Vision request failed:"
                           << QString::fromNSString(error.localizedDescription);
                CGImageRelease(cgImage);
                result.status = Status::Failed;
                return result;
            }

            VNInstanceMaskObservation* observation = request.results.firstObject;
            if (!observation || observation.allInstances.count == 0) {
                CGImageRelease(cgImage);
                result.status = Status::NoSubject;
                return result;
            }

            CVPixelBufferRef buffer =
                [observation generateScaledMaskForImageForInstances:observation.allInstances
                                                 fromRequestHandler:handler
                                                              error:&error];
            if (!buffer) {
                qWarning() << "ForegroundSegmentation: mask generation failed:"
                           << QString::fromNSString(error.localizedDescription);
                CGImageRelease(cgImage);
                result.status = Status::Failed;
                return result;
            }

            result.mask = grayscaleMaskFromBuffer(buffer, image.size());
            CVPixelBufferRelease(buffer);
            CGImageRelease(cgImage);
            result.status = result.mask.isNull() ? Status::Failed : Status::Success;
            return result;
        }
    }

    result.status = Status::Unsupported;
    return result;
}

} // namespace SnapTray
