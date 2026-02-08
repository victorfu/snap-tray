#include "OCRManager.h"

#include <QDebug>
#include <QImage>
#include <QPointer>
#include <utility>

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AppKit/AppKit.h>

namespace {

// Language code to display name mapping
NSDictionary<NSString*, NSArray<NSString*>*>* getLanguageNameMap()
{
    return @{
        @"en-US": @[@"English", @"English"],
        @"en-GB": @[@"English (UK)", @"English (UK)"],
        @"zh-Hant": @[@"繁體中文", @"Traditional Chinese"],
        @"zh-Hans": @[@"简体中文", @"Simplified Chinese"],
        @"ja-JP": @[@"日本語", @"Japanese"],
        @"ko-KR": @[@"한국어", @"Korean"],
        @"fr-FR": @[@"Français", @"French"],
        @"de-DE": @[@"Deutsch", @"German"],
        @"es-ES": @[@"Español", @"Spanish"],
        @"pt-BR": @[@"Português (Brasil)", @"Portuguese (Brazil)"],
        @"pt-PT": @[@"Português", @"Portuguese"],
        @"it-IT": @[@"Italiano", @"Italian"],
        @"nl-NL": @[@"Nederlands", @"Dutch"],
        @"pl-PL": @[@"Polski", @"Polish"],
        @"ru-RU": @[@"Русский", @"Russian"],
        @"uk-UA": @[@"Українська", @"Ukrainian"],
        @"tr-TR": @[@"Türkçe", @"Turkish"],
        @"th-TH": @[@"ไทย", @"Thai"],
        @"vi-VN": @[@"Tiếng Việt", @"Vietnamese"],
        @"ar-SA": @[@"العربية", @"Arabic"],
        @"he-IL": @[@"עברית", @"Hebrew"],
        @"hi-IN": @[@"हिन्दी", @"Hindi"],
        @"id-ID": @[@"Bahasa Indonesia", @"Indonesian"],
        @"ms-MY": @[@"Bahasa Melayu", @"Malay"],
        @"sv-SE": @[@"Svenska", @"Swedish"],
        @"da-DK": @[@"Dansk", @"Danish"],
        @"fi-FI": @[@"Suomi", @"Finnish"],
        @"nb-NO": @[@"Norsk", @"Norwegian"],
        @"cs-CZ": @[@"Čeština", @"Czech"],
        @"el-GR": @[@"Ελληνικά", @"Greek"],
        @"hu-HU": @[@"Magyar", @"Hungarian"],
        @"ro-RO": @[@"Română", @"Romanian"],
        @"sk-SK": @[@"Slovenčina", @"Slovak"],
        @"ca-ES": @[@"Català", @"Catalan"],
        @"hr-HR": @[@"Hrvatski", @"Croatian"],
    };
}

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

QRectF convertVisionBoundingBox(const CGRect &visionRect)
{
    const qreal x = static_cast<qreal>(visionRect.origin.x);
    const qreal width = static_cast<qreal>(visionRect.size.width);
    const qreal height = static_cast<qreal>(visionRect.size.height);
    // Vision uses a bottom-left origin. Convert to top-left to match Qt/UI usage.
    const qreal y = 1.0 - static_cast<qreal>(visionRect.origin.y) - height;
    return QRectF(x, y, width, height).normalized();
}

} // anonymous namespace

OCRManager::OCRManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<OCRResult>("OCRResult");
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
            OCRResult result;
            result.success = false;
            result.error = QStringLiteral("Invalid pixmap");
            callback(result);
        }
        return;
    }

    if (@available(macOS 10.15, *)) {
        // Create CGImage from QPixmap
        CGImageRef cgImage = createCGImageFromPixmap(pixmap);
        if (!cgImage) {
            if (callback) {
                OCRResult result;
                result.success = false;
                result.error = QStringLiteral("Failed to create CGImage");
                callback(result);
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
                OCRResult result;

                if (error) {
                    result.error = QString::fromNSString([error localizedDescription]);
                    qDebug() << "OCRManager: Recognition failed:" << result.error;
                } else {
                    NSArray<VNRecognizedTextObservation *> *observations = request.results;
                    NSMutableString *fullText = [NSMutableString string];
                    QVector<OCRTextBlock> blocks;
                    blocks.reserve(observations.count);

                    for (VNRecognizedTextObservation *observation in observations) {
                        VNRecognizedText *topCandidate = [[observation topCandidates:1] firstObject];
                        if (topCandidate) {
                            const QString candidateText = QString::fromNSString(topCandidate.string).trimmed();
                            if (candidateText.isEmpty()) {
                                continue;
                            }

                            if ([fullText length] > 0) {
                                [fullText appendString:@"\n"];
                            }
                            [fullText appendString:topCandidate.string];

                            OCRTextBlock block;
                            block.text = candidateText;
                            block.boundingRect = convertVisionBoundingBox(observation.boundingBox);
                            block.confidence = topCandidate.confidence;
                            blocks.push_back(block);
                        }
                    }

                    result.text = QString::fromNSString(fullText).trimmed();
                    result.blocks = std::move(blocks);
                    result.success = !result.text.isEmpty();

                    if (!result.success) {
                        result.error = QStringLiteral("No text detected");
                    }

                    qDebug() << "OCRManager: Recognition complete, found"
                             << observations.count << "text observations";
                }

                // Dispatch result to main thread
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (callbackCopy) {
                        callbackCopy(result);
                    }
                    if (weakThis) {
                        emit weakThis->recognitionComplete(result);
                    }
                });
            }];

        // Configure request for accurate recognition
        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;

        // Build language array from settings
        NSMutableArray<NSString*> *langArray = [NSMutableArray array];
        for (const QString &lang : m_languages) {
            [langArray addObject:lang.toNSString()];
        }
        request.recognitionLanguages = langArray;

        // Enable language correction for better accuracy
        request.usesLanguageCorrection = YES;

        qDebug() << "OCRManager: Starting text recognition with languages:" << m_languages;

        // Perform request asynchronously
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSError *performError = nil;
            BOOL performed = [handler performRequests:@[request] error:&performError];

            if (!performed && performError) {
                OCRResult result;
                result.success = false;
                result.error = QString::fromNSString([performError localizedDescription]);
                qDebug() << "OCRManager: Failed to perform request:" << result.error;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (callbackCopy) {
                        callbackCopy(result);
                    }
                    if (weakThis) {
                        emit weakThis->recognitionComplete(result);
                    }
                });
            }
        });
    } else {
        if (callback) {
            OCRResult result;
            result.success = false;
            result.error = QStringLiteral("Vision Framework not available (requires macOS 10.15+)");
            callback(result);
        }
    }
}

QList<OCRLanguageInfo> OCRManager::availableLanguages()
{
    QList<OCRLanguageInfo> result;

    if (@available(macOS 10.15, *)) {
        NSError *error = nil;
        NSArray<NSString *> *supported = nil;

        // Use modern instance method for macOS 12+, fall back to deprecated class method for older
        if (@available(macOS 12.0, *)) {
            // Create a temporary request to query supported languages
            VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
            request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
            supported = [request supportedRecognitionLanguagesAndReturnError:&error];
        } else {
            // Fall back to deprecated API for macOS 10.15-11.x
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            supported = [VNRecognizeTextRequest
                supportedRecognitionLanguagesForTextRecognitionLevel:VNRequestTextRecognitionLevelAccurate
                revision:VNRecognizeTextRequestRevision2
                error:&error];
#pragma clang diagnostic pop
        }

        if (error) {
            qDebug() << "OCRManager: Failed to get supported languages:"
                     << QString::fromNSString([error localizedDescription]);
            return result;
        }

        NSDictionary *nameMap = getLanguageNameMap();

        for (NSString *code in supported) {
            OCRLanguageInfo info;
            info.code = QString::fromNSString(code);

            NSArray<NSString*> *names = nameMap[code];
            if (names && names.count >= 2) {
                info.nativeName = QString::fromNSString(names[0]);
                info.englishName = QString::fromNSString(names[1]);
            } else {
                // Fallback: use code as name
                info.nativeName = info.code;
                info.englishName = info.code;
            }

            result.append(info);
        }

        qDebug() << "OCRManager: Found" << result.size() << "supported languages";
    }

    return result;
}

void OCRManager::setRecognitionLanguages(const QStringList &languageCodes)
{
    m_languages = languageCodes;
    qDebug() << "OCRManager: Recognition languages set to:" << m_languages;
}

QStringList OCRManager::recognitionLanguages() const
{
    return m_languages;
}
