#include "AVFoundationEncoder.h"

#ifdef Q_OS_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <AudioToolbox/AudioToolbox.h>
#include <QFile>
#include <QDebug>

class AVFoundationEncoderPrivate
{
public:
    AVAssetWriter *assetWriter = nil;
    AVAssetWriterInput *videoInput = nil;
    AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;

    QString outputPath;
    QString lastError;
    QSize frameSize;
    int frameRate = 30;
    qint64 framesWritten = 0;
    qint64 frameNumber = 0;
    bool running = false;
    int quality = 55;  // 0-100

    // Audio members
    AVAssetWriterInput *audioInput = nil;
    bool audioEnabled = false;
    int audioSampleRate = 48000;
    int audioChannels = 2;
    int audioBitsPerSample = 16;
    qint64 audioSamplesWritten = 0;

    int calculateBitrate() const {
        int pixels = frameSize.width() * frameSize.height();
        // Quality 0-100 maps to bits per pixel 0.1-0.3
        double bitsPerPixel = 0.1 + (quality / 100.0) * 0.2;
        int bitrate = static_cast<int>(pixels * frameRate * bitsPerPixel);
        // Clamp to reasonable range: 1 Mbps to 50 Mbps
        return qBound(1000000, bitrate, 50000000);
    }

    CVPixelBufferRef createPixelBufferFromImage(const QImage &image) {
        // Convert to ARGB32 format which maps well to BGRA
        QImage converted = image.convertToFormat(QImage::Format_ARGB32);

        CVPixelBufferRef pixelBuffer = nullptr;
        NSDictionary *attributes = @{
            (NSString *)kCVPixelBufferCGImageCompatibilityKey: @YES,
            (NSString *)kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
            (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };

        CVReturn result = CVPixelBufferCreate(
            kCFAllocatorDefault,
            frameSize.width(),
            frameSize.height(),
            kCVPixelFormatType_32BGRA,
            (__bridge CFDictionaryRef)attributes,
            &pixelBuffer
        );

        if (result != kCVReturnSuccess || !pixelBuffer) {
            qWarning() << "AVFoundationEncoder: Failed to create pixel buffer:" << result;
            return nullptr;
        }

        CVPixelBufferLockBaseAddress(pixelBuffer, 0);

        void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

        const uchar *srcData = converted.constBits();
        size_t srcBytesPerRow = static_cast<size_t>(converted.bytesPerLine());

        // Copy row by row, handling stride differences
        for (int y = 0; y < frameSize.height(); y++) {
            memcpy(
                static_cast<uint8_t *>(baseAddress) + y * bytesPerRow,
                srcData + y * srcBytesPerRow,
                qMin(bytesPerRow, srcBytesPerRow)
            );
        }

        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
        return pixelBuffer;
    }

    void cleanup() {
        videoInput = nil;
        adaptor = nil;
        audioInput = nil;
        assetWriter = nil;
        running = false;
        audioEnabled = false;
        audioSamplesWritten = 0;
    }
};

AVFoundationEncoder::AVFoundationEncoder(QObject *parent)
    : IVideoEncoder(parent)
    , d(new AVFoundationEncoderPrivate)
{
}

AVFoundationEncoder::~AVFoundationEncoder()
{
    abort();
    delete d;
}

bool AVFoundationEncoder::isAvailable() const
{
    // AVFoundation is available on macOS 10.7+
    // We require 10.15+ for SnapTray anyway
    return true;
}

bool AVFoundationEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    if (d->running) {
        d->lastError = "Encoder already running";
        return false;
    }

    // Ensure dimensions are even (required for H.264)
    QSize adjustedSize = frameSize;
    if (adjustedSize.width() % 2 != 0) {
        adjustedSize.setWidth(adjustedSize.width() + 1);
    }
    if (adjustedSize.height() % 2 != 0) {
        adjustedSize.setHeight(adjustedSize.height() + 1);
    }

    d->outputPath = outputPath;
    d->frameSize = adjustedSize;
    d->frameRate = frameRate;
    d->framesWritten = 0;
    d->frameNumber = 0;

    // Remove existing file
    QFile::remove(outputPath);

    NSError *error = nil;
    NSURL *outputURL = [NSURL fileURLWithPath:outputPath.toNSString()];

    d->assetWriter = [[AVAssetWriter alloc] initWithURL:outputURL
                                               fileType:AVFileTypeMPEG4
                                                  error:&error];

    if (error || !d->assetWriter) {
        d->lastError = QString("Failed to create AVAssetWriter: %1")
            .arg(QString::fromNSString(error.localizedDescription));
        return false;
    }

    // Configure H.264 video settings
    int bitrate = d->calculateBitrate();

    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(adjustedSize.width()),
        AVVideoHeightKey: @(adjustedSize.height()),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(bitrate),
            AVVideoExpectedSourceFrameRateKey: @(frameRate),
            AVVideoMaxKeyFrameIntervalKey: @(frameRate * 2),
            AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            AVVideoAllowFrameReorderingKey: @NO
        }
    };

    d->videoInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo
                                                   outputSettings:videoSettings];
    d->videoInput.expectsMediaDataInRealTime = YES;

    NSDictionary *sourcePixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(adjustedSize.width()),
        (NSString *)kCVPixelBufferHeightKey: @(adjustedSize.height())
    };

    d->adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
                  initWithAssetWriterInput:d->videoInput
                  sourcePixelBufferAttributes:sourcePixelBufferAttributes];

    if (![d->assetWriter canAddInput:d->videoInput]) {
        d->lastError = "Cannot add video input to asset writer";
        d->cleanup();
        return false;
    }

    [d->assetWriter addInput:d->videoInput];

    // Configure audio input if enabled
    if (d->audioEnabled) {
        // Audio output settings (AAC encoding)
        AudioChannelLayout channelLayout = {0};
        channelLayout.mChannelLayoutTag = (d->audioChannels == 1)
            ? kAudioChannelLayoutTag_Mono
            : kAudioChannelLayoutTag_Stereo;

        NSDictionary *audioSettings = @{
            AVFormatIDKey: @(kAudioFormatMPEG4AAC),
            AVSampleRateKey: @(d->audioSampleRate),
            AVNumberOfChannelsKey: @(d->audioChannels),
            AVEncoderBitRateKey: @(128000),  // 128 kbps
            AVChannelLayoutKey: [NSData dataWithBytes:&channelLayout
                                               length:sizeof(AudioChannelLayout)]
        };

        d->audioInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                       outputSettings:audioSettings];
        d->audioInput.expectsMediaDataInRealTime = YES;

        if ([d->assetWriter canAddInput:d->audioInput]) {
            [d->assetWriter addInput:d->audioInput];
            qDebug() << "AVFoundationEncoder: Audio input added -"
                     << d->audioSampleRate << "Hz," << d->audioChannels << "ch";
        } else {
            qWarning() << "AVFoundationEncoder: Cannot add audio input, continuing without audio";
            d->audioInput = nil;
            d->audioEnabled = false;
        }
    }

    if (![d->assetWriter startWriting]) {
        d->lastError = QString("Failed to start writing: %1")
            .arg(QString::fromNSString(d->assetWriter.error.localizedDescription));
        d->cleanup();
        return false;
    }

    [d->assetWriter startSessionAtSourceTime:kCMTimeZero];

    d->running = true;
    qDebug() << "AVFoundationEncoder: Started encoding to" << outputPath
             << "size:" << d->frameSize << "fps:" << frameRate
             << "quality:" << d->quality << "bitrate:" << bitrate;
    return true;
}

void AVFoundationEncoder::writeFrame(const QImage &frame, qint64 timestampMs)
{
    if (!d->running || !d->videoInput || !d->adaptor) {
        return;
    }

    // Check if input is ready for more data
    if (!d->videoInput.readyForMoreMediaData) {
        // Skip frame if encoder is busy
        return;
    }

    // Scale frame if needed
    QImage scaledFrame = frame;
    if (frame.size() != d->frameSize) {
        scaledFrame = frame.scaled(d->frameSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    CVPixelBufferRef pixelBuffer = d->createPixelBufferFromImage(scaledFrame);
    if (!pixelBuffer) {
        return;
    }

    // Calculate presentation time
    CMTime presentationTime;
    if (timestampMs >= 0) {
        presentationTime = CMTimeMake(timestampMs, 1000);
    } else {
        presentationTime = CMTimeMake(d->frameNumber, d->frameRate);
    }

    BOOL success = [d->adaptor appendPixelBuffer:pixelBuffer
                            withPresentationTime:presentationTime];

    CVPixelBufferRelease(pixelBuffer);

    if (success) {
        d->framesWritten++;
        d->frameNumber++;
        emit progress(d->framesWritten);
    } else {
        qWarning() << "AVFoundationEncoder: Failed to append pixel buffer";
    }
}

void AVFoundationEncoder::finish()
{
    if (!d->running) {
        emit finished(false, QString());
        return;
    }

    d->running = false;
    [d->videoInput markAsFinished];
    if (d->audioInput) {
        [d->audioInput markAsFinished];
    }

    __block bool completed = false;
    __block QString errorMsg;

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    [d->assetWriter finishWritingWithCompletionHandler:^{
        if (d->assetWriter.status == AVAssetWriterStatusCompleted) {
            completed = true;
        } else if (d->assetWriter.error) {
            errorMsg = QString::fromNSString(d->assetWriter.error.localizedDescription);
        }
        dispatch_semaphore_signal(semaphore);
    }];

    // Wait up to 30 seconds for completion
    dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC);
    long result = dispatch_semaphore_wait(semaphore, timeout);

    QString outputPath = d->outputPath;
    qint64 framesWritten = d->framesWritten;
    d->cleanup();

    if (result != 0) {
        d->lastError = "Timeout waiting for encoding to complete";
        emit error(d->lastError);
        emit finished(false, QString());
    } else if (completed) {
        qDebug() << "AVFoundationEncoder: Finished successfully, frames:" << framesWritten;
        emit finished(true, outputPath);
    } else {
        d->lastError = errorMsg.isEmpty() ? "Encoding failed" : errorMsg;
        emit error(d->lastError);
        emit finished(false, QString());
    }
}

void AVFoundationEncoder::abort()
{
    if (!d->running) {
        return;
    }

    QString outputPath = d->outputPath;
    [d->assetWriter cancelWriting];
    d->cleanup();

    // Remove partial output file
    QFile::remove(outputPath);
    qDebug() << "AVFoundationEncoder: Aborted, removed" << outputPath;
}

bool AVFoundationEncoder::isRunning() const
{
    return d->running;
}

QString AVFoundationEncoder::lastError() const
{
    return d->lastError;
}

qint64 AVFoundationEncoder::framesWritten() const
{
    return d->framesWritten;
}

QString AVFoundationEncoder::outputPath() const
{
    return d->outputPath;
}

void AVFoundationEncoder::setQuality(int quality)
{
    d->quality = qBound(0, quality, 100);
}

void AVFoundationEncoder::setAudioFormat(int sampleRate, int channels, int bitsPerSample)
{
    if (d->running) {
        qWarning() << "AVFoundationEncoder: Cannot set audio format while running";
        return;
    }
    d->audioEnabled = true;
    d->audioSampleRate = sampleRate;
    d->audioChannels = channels;
    d->audioBitsPerSample = bitsPerSample;
    qDebug() << "AVFoundationEncoder: Audio format set -" << sampleRate << "Hz,"
             << channels << "ch," << bitsPerSample << "bit";
}

bool AVFoundationEncoder::isAudioSupported() const
{
    return true;  // AVFoundation supports audio encoding
}

void AVFoundationEncoder::writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs)
{
    if (!d->running || !d->audioInput || !d->audioEnabled) {
        return;
    }

    // Check if input is ready for more data
    if (!d->audioInput.readyForMoreMediaData) {
        return;  // Skip if encoder is busy
    }

    // Create audio sample buffer
    CMBlockBufferRef blockBuffer = nullptr;
    size_t dataLength = static_cast<size_t>(pcmData.size());

    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        nullptr,  // Will allocate memory
        dataLength,
        kCFAllocatorDefault,
        nullptr,
        0,
        dataLength,
        kCMBlockBufferAssureMemoryNowFlag,
        &blockBuffer
    );

    if (status != kCMBlockBufferNoErr || !blockBuffer) {
        qWarning() << "AVFoundationEncoder: Failed to create block buffer:" << status;
        return;
    }

    // Copy PCM data into block buffer
    status = CMBlockBufferReplaceDataBytes(
        pcmData.constData(),
        blockBuffer,
        0,
        dataLength
    );

    if (status != kCMBlockBufferNoErr) {
        CFRelease(blockBuffer);
        qWarning() << "AVFoundationEncoder: Failed to copy audio data:" << status;
        return;
    }

    // Create audio format description
    AudioStreamBasicDescription asbd = {0};
    asbd.mSampleRate = d->audioSampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = d->audioChannels * (d->audioBitsPerSample / 8);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = asbd.mBytesPerPacket;
    asbd.mChannelsPerFrame = d->audioChannels;
    asbd.mBitsPerChannel = d->audioBitsPerSample;

    CMAudioFormatDescriptionRef formatDesc = nullptr;
    status = CMAudioFormatDescriptionCreate(
        kCFAllocatorDefault,
        &asbd,
        0,
        nullptr,
        0,
        nullptr,
        nullptr,
        &formatDesc
    );

    if (status != noErr || !formatDesc) {
        CFRelease(blockBuffer);
        qWarning() << "AVFoundationEncoder: Failed to create audio format description:" << status;
        return;
    }

    // Calculate number of samples
    int bytesPerFrame = d->audioChannels * (d->audioBitsPerSample / 8);
    CMItemCount numSamples = static_cast<CMItemCount>(dataLength / bytesPerFrame);

    // Create timing info
    CMTime presentationTime = CMTimeMake(timestampMs, 1000);
    CMTime duration = CMTimeMake(numSamples, d->audioSampleRate);

    CMSampleTimingInfo timingInfo = {0};
    timingInfo.duration = duration;
    timingInfo.presentationTimeStamp = presentationTime;
    timingInfo.decodeTimeStamp = kCMTimeInvalid;

    // Create sample buffer
    CMSampleBufferRef sampleBuffer = nullptr;
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        blockBuffer,
        true,  // dataReady
        nullptr,
        nullptr,
        formatDesc,
        numSamples,
        1,  // numSampleTimingEntries
        &timingInfo,
        0,  // numSampleSizeEntries
        nullptr,
        &sampleBuffer
    );

    CFRelease(blockBuffer);
    CFRelease(formatDesc);

    if (status != noErr || !sampleBuffer) {
        qWarning() << "AVFoundationEncoder: Failed to create sample buffer:" << status;
        return;
    }

    // Append sample buffer to audio input
    BOOL success = [d->audioInput appendSampleBuffer:sampleBuffer];
    CFRelease(sampleBuffer);

    if (success) {
        d->audioSamplesWritten++;
    } else {
        qWarning() << "AVFoundationEncoder: Failed to append audio sample buffer";
    }
}

#endif // Q_OS_MAC
