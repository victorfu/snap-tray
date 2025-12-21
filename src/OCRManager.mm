#include "OCRManager.h"

#include <QDebug>
#include <QImage>
#include <QPointer>

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>

namespace {

CGImageRef createCGImageFromPixmap(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        return nullptr;
    }

    // Convert QPixmap -> QImage in RGBA format
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);

    if (image.isNull()) {
        return nullptr;
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) {
        return nullptr;
    }

    // Create data provider from image bytes
    CFDataRef data = CFDataCreate(nullptr, image.bits(), image.sizeInBytes());
    if (!data) {
        CGColorSpaceRelease(colorSpace);
        return nullptr;
    }

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CFRelease(data);

    if (!provider) {
        CGColorSpaceRelease(colorSpace);
        return nullptr;
    }

    CGImageRef cgImage = CGImageCreate(
        image.width(),
        image.height(),
        8,                                  // bits per component
        32,                                 // bits per pixel
        image.bytesPerLine(),           // bytes per row
        colorSpace,
        kCGBitmapByteOrderDefault | kCGImageAlphaLast,
        provider,
        nullptr,                            // decode array
        false,                              // should interpolate
        kCGRenderingIntentDefault
    );

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);

    return cgImage;  // Caller must release
}

} // anonymous namespace

OCRManager::OCRManager(QObject *parent)
    : QObject(parent)
{
}

OCRManager::~OCRManager()
{
}

bool OCRManager::isAvailable()
{
    if (@available(macOS 10.15, *)) {
        return true;
    }
    return false;
}

void OCRManager::recognizeText(const QPixmap &pixmap, const OCRCallback &callback)
{
    if (pixmap.isNull()) {
        if (callback) {
            callback(false, QString(), QStringLiteral("Invalid pixmap"));
        }
        return;
    }

    if (@available(macOS 10.15, *)) {
        // Create CGImage from QPixmap
        CGImageRef cgImage = createCGImageFromPixmap(pixmap);
        if (!cgImage) {
            if (callback) {
                callback(false, QString(), QStringLiteral("Failed to create CGImage"));
            }
            return;
        }

        // Prevent OCRManager from being destroyed while async operation is in progress
        QPointer<OCRManager> weakThis = this;

        // Copy callback for use in block
        OCRCallback callbackCopy = callback;

        // Create request handler
        VNImageRequestHandler *handler = [[VNImageRequestHandler alloc]
            initWithCGImage:cgImage
            options:@{}];

        // Release CGImage now - handler has retained it
        CGImageRelease(cgImage);

        // Create text recognition request
        VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc]
            initWithCompletionHandler:^(VNRequest * _Nonnull request, NSError * _Nullable error) {
                QString resultText;
                QString errorMsg;
                bool success = false;

                if (error) {
                    errorMsg = QString::fromNSString([error localizedDescription]);
                    qDebug() << "OCRManager: Recognition failed:" << errorMsg;
                } else {
                    NSArray<VNRecognizedTextObservation *> *observations = request.results;
                    NSMutableString *fullText = [NSMutableString string];

                    for (VNRecognizedTextObservation *observation in observations) {
                        VNRecognizedText *topCandidate = [[observation topCandidates:1] firstObject];
                        if (topCandidate) {
                            if ([fullText length] > 0) {
                                [fullText appendString:@"\n"];
                            }
                            [fullText appendString:topCandidate.string];
                        }
                    }

                    resultText = QString::fromNSString(fullText).trimmed();
                    success = !resultText.isEmpty();

                    if (!success) {
                        errorMsg = QStringLiteral("No text detected");
                    }

                    qDebug() << "OCRManager: Recognition complete, found"
                             << observations.count << "text observations";
                }

                // Dispatch result to main thread
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (callbackCopy) {
                        callbackCopy(success, resultText, errorMsg);
                    }
                    if (weakThis) {
                        emit weakThis->recognitionComplete(success, resultText, errorMsg);
                    }
                });
            }];

        // Configure request for accurate recognition
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;

        // Set recognition languages: Traditional Chinese, Simplified Chinese, English
        request.recognitionLanguages = @[@"zh-Hant", @"zh-Hans", @"en-US"];

        // Enable language correction for better accuracy
        request.usesLanguageCorrection = YES;

        qDebug() << "OCRManager: Starting text recognition...";

        // Perform request asynchronously
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSError *performError = nil;
            BOOL performed = [handler performRequests:@[request] error:&performError];

            if (!performed && performError) {
                QString errorMsg = QString::fromNSString([performError localizedDescription]);
                qDebug() << "OCRManager: Failed to perform request:" << errorMsg;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (callbackCopy) {
                        callbackCopy(false, QString(), errorMsg);
                    }
                });
            }
        });
    } else {
        if (callback) {
            callback(false, QString(), QStringLiteral("Vision Framework not available (requires macOS 10.15+)"));
        }
    }
}
