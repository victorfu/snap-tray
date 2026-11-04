#include "video/NativeEditExport.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <VideoToolbox/VideoToolbox.h>

#include <QElapsedTimer>
#include <QThread>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <atomic>
#include <cstring>

namespace SnapTray {
namespace {
NSURL* url(const QString& path) { return [NSURL fileURLWithPath:path.toNSString()]; }

class AVEditMedia final : public NativeEditMedia, private NativePcmReader {
public:
    ~AVEditMedia() override { abort(); }
    QString error() const override { QMutexLocker lock(&m_errorMutex); return m_error; }
    bool concurrentAudio() const override { return true; }
    void cancel() override { m_cancelled = true; }

    bool open(const QString& input, const QString& output,
              const NativeEditFormat& format, bool decodeAudio) override
    {
        @autoreleasepool {
            m_format = format;
            NSError* error = nil;
            m_writer = [[AVAssetWriter alloc] initWithURL:url(output) fileType:AVFileTypeMPEG4 error:&error];
            if (!m_writer) return fail(error, "Cannot create edited MP4 writer.");
            NSMutableDictionary* compression = [@{
                AVVideoAverageBitRateKey: @(format.bitrate),
                AVVideoExpectedSourceFrameRateKey: @(format.fps),
                AVVideoMaxKeyFrameIntervalKey: @(qRound(format.fps * 2)),
                AVVideoAllowFrameReorderingKey: @NO
            } mutableCopy];
            if (!format.hevc) compression[AVVideoProfileLevelKey] = AVVideoProfileLevelH264HighAutoLevel;
            m_video = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:@{
                AVVideoCodecKey: format.hevc ? AVVideoCodecTypeHEVC : AVVideoCodecTypeH264,
                AVVideoWidthKey: @(format.size.width()), AVVideoHeightKey: @(format.size.height()),
                AVVideoCompressionPropertiesKey: compression
            }];
            m_video.expectsMediaDataInRealTime = NO;
            if (![m_writer canAddInput:m_video]) return fail(nil, "Cannot configure edited video output.");
            [m_writer addInput:m_video];
            m_pixels = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:m_video
                sourcePixelBufferAttributes:@{
                    (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                    (NSString*)kCVPixelBufferWidthKey: @(format.size.width()),
                    (NSString*)kCVPixelBufferHeightKey: @(format.size.height())
                }];
            if (format.audio) {
                m_audio = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:@{
                    AVFormatIDKey: @(kAudioFormatMPEG4AAC), AVSampleRateKey: @48000,
                    AVNumberOfChannelsKey: @2, AVEncoderBitRateKey: @128000
                }];
                m_audio.expectsMediaDataInRealTime = NO;
                if (![m_writer canAddInput:m_audio]) return fail(nil, "Cannot configure edited AAC output.");
                [m_writer addInput:m_audio];
                AudioStreamBasicDescription pcm{};
                pcm.mSampleRate = 48000;
                pcm.mFormatID = kAudioFormatLinearPCM;
                pcm.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
                pcm.mBytesPerPacket = pcm.mBytesPerFrame = 4;
                pcm.mFramesPerPacket = 1;
                pcm.mChannelsPerFrame = 2;
                pcm.mBitsPerChannel = 16;
                if (CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &pcm, 0, nullptr, 0, nullptr,
                                                  nullptr, &m_pcmFormat) != noErr)
                    return fail(nil, "Cannot describe edited PCM audio.");
            }
            if (decodeAudio) {
                AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url(input) options:nil];
                AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeAudio].firstObject;
                if (!track) return fail(nil, "Source audio track is missing.");
                m_reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
                if (!m_reader) return fail(error, "Cannot create source audio reader.");
                // AudioMixOutput supports sample-rate/channel conversion; a
                // TrackOutput alone does not promise it for every source.
                m_decodedAudio = [AVAssetReaderAudioMixOutput assetReaderAudioMixOutputWithAudioTracks:@[track]
                    audioSettings:@{
                        AVFormatIDKey: @(kAudioFormatLinearPCM), AVSampleRateKey: @48000,
                        AVNumberOfChannelsKey: @2, AVLinearPCMBitDepthKey: @16,
                        AVLinearPCMIsFloatKey: @NO, AVLinearPCMIsBigEndianKey: @NO,
                        AVLinearPCMIsNonInterleaved: @NO
                    }];
                if (![m_reader canAddOutput:m_decodedAudio]) return fail(nil, "Cannot decode source audio.");
                [m_reader addOutput:m_decodedAudio];
                if (![m_reader startReading]) return fail(m_reader.error, "Cannot start source audio reader.");
            }
            if (![m_writer startWriting]) return fail(m_writer.error, "Cannot start edited MP4 writer.");
            [m_writer startSessionAtSourceTime:kCMTimeZero];
            return true;
        }
    }

    QByteArray audioAt(qint64 frame, int count) override {
        QMutexLocker lock(&m_errorMutex);
        return read(frame, count);
    }

    bool writeVideo(const QImage& image, qint64 timeUs, qint64 durationUs) override
    {
        @autoreleasepool {
            if (!ready(m_video)) return false;
            CVPixelBufferRef buffer = nullptr;
            if (!m_pixels.pixelBufferPool || CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,
                    m_pixels.pixelBufferPool, &buffer) != kCVReturnSuccess)
                return fail(nil, "Cannot allocate edited video frame.");
            const auto lock = CVPixelBufferLockBaseAddress(buffer, 0);
            if (lock != kCVReturnSuccess) {
                CVPixelBufferRelease(buffer);
                return fail(nil, "Cannot lock edited video frame.");
            }
            const QImage bgra = image.convertToFormat(QImage::Format_RGB32);
            auto* bytes = static_cast<unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
            const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
            for (int y = 0; y < image.height(); ++y)
                std::memcpy(bytes + y * stride, bgra.constScanLine(y), size_t(image.width()) * 4);
            CVPixelBufferUnlockBaseAddress(buffer, 0);
            const bool ok = [m_pixels appendPixelBuffer:buffer withPresentationTime:CMTimeMake(timeUs, 1000000)];
            CVPixelBufferRelease(buffer);
            if (!ok) return fail(m_writer.error, "Cannot encode edited video frame.");
            // The exporter writes the remaining audio after this last picture.
            // Tell AVAssetWriter the video has ended now: otherwise its audio
            // input can wait for more video to interleave, while finish() is
            // itself waiting for that audio to be accepted.
            if (timeUs + durationUs >= m_format.durationMs * 1000) {
                [m_video markAsFinished];
                m_videoFinished = true;
            }
            return true;
        }
    }

    bool writeAudio(const QByteArray& pcm, qint64 startFrame) override
    {
        @autoreleasepool {
            if (!ready(m_audio)) return false;
            CMBlockBufferRef block = nullptr;
            CMSampleBufferRef sample = nullptr;
            OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, pcm.size(),
                kCFAllocatorDefault, nullptr, 0, pcm.size(), 0, &block);
            if (status == noErr) status = CMBlockBufferReplaceDataBytes(pcm.constData(), block, 0, pcm.size());
            if (status == noErr) status = CMAudioSampleBufferCreateWithPacketDescriptions(kCFAllocatorDefault,
                block, true, nullptr, nullptr, m_pcmFormat, pcm.size() / 4,
                CMTimeMake(startFrame, 48000), nullptr, &sample);
            const bool ok = status == noErr && [m_audio appendSampleBuffer:sample];
            if (sample) CFRelease(sample);
            if (block) CFRelease(block);
            if (!ok) return fail(m_writer.error, "Cannot encode edited audio.");
            if (startFrame + pcm.size() / 4 >= m_format.durationMs * 48) {
                [m_audio markAsFinished];
                m_audioFinished = true;
            }
            return true;
        }
    }

    bool finish() override
    {
        @autoreleasepool {
            if (!m_videoFinished) [m_video markAsFinished];
            if (m_audio && !m_audioFinished) [m_audio markAsFinished];
            [m_writer endSessionAtSourceTime:CMTimeMake(m_format.durationMs, 1000)];
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            [m_writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) != 0)
                return fail(nil, "Timed out finishing edited MP4.");
            return m_writer.status == AVAssetWriterStatusCompleted || fail(m_writer.error, "Cannot finish edited MP4.");
        }
    }

    void abort() override
    {
        @autoreleasepool {
            if (m_reader.status == AVAssetReaderStatusReading) [m_reader cancelReading];
            if (m_writer.status == AVAssetWriterStatusWriting) [m_writer cancelWriting];
            m_reader = nil; m_decodedAudio = nil;
            m_pixels = nil; m_video = nil; m_audio = nil; m_writer = nil;
            m_videoFinished = false;
            m_audioFinished = false;
            if (m_pcmFormat) { CFRelease(m_pcmFormat); m_pcmFormat = nullptr; }
        }
    }

private:
    bool fail(NSError* error, const char* fallback)
    {
        QMutexLocker lock(&m_errorMutex);
        if (m_error.isEmpty())
            m_error = error ? QString::fromNSString(error.localizedDescription) : QString::fromLatin1(fallback);
        return false;
    }
    bool ready(AVAssetWriterInput* input)
    {
        QElapsedTimer timeout; timeout.start();
        while (!m_cancelled && input && !input.readyForMoreMediaData && m_writer.status == AVAssetWriterStatusWriting
               && timeout.elapsed() < 10000) QThread::msleep(1);
        if (!m_cancelled && input && input.readyForMoreMediaData && m_writer.status == AVAssetWriterStatusWriting) return true;
        QMutexLocker lock(&m_errorMutex);
        fail(m_writer.error, "Edited MP4 writer stopped accepting data.");
        m_error += QStringLiteral(" (%1, writer status %2, waited %3 ms)")
            .arg(input == m_video ? QStringLiteral("video") : QStringLiteral("audio"))
            .arg(int(m_writer.status)).arg(timeout.elapsed());
        return false;
    }
    bool nextPacket(QByteArray& pcm, qint64& startFrame) override
    {
        @autoreleasepool {
            CMSampleBufferRef sample = [m_decodedAudio copyNextSampleBuffer];
            if (!sample) {
                if (m_reader.status != AVAssetReaderStatusCompleted) fail(m_reader.error, "Cannot decode source audio.");
                return false;
            }
            CMBlockBufferRef data = CMSampleBufferGetDataBuffer(sample);
            const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
            bool ok = CMTIME_IS_NUMERIC(pts);
            if (data && ok) {
                pcm.resize(qsizetype(CMBlockBufferGetDataLength(data)));
                ok = CMBlockBufferCopyDataBytes(data, 0, pcm.size(), pcm.data()) == noErr;
                startFrame = CMTimeConvertScale(pts, 48000, kCMTimeRoundingMethod_RoundHalfAwayFromZero).value;
            } else if (CMSampleBufferGetNumSamples(sample) > 0) ok = false;
            CFRelease(sample);
            return ok || fail(nil, "Invalid decoded audio sample.");
        }
    }
    NativeEditFormat m_format;
    mutable QRecursiveMutex m_errorMutex;
    std::atomic_bool m_cancelled{false};
    bool m_videoFinished = false;
    bool m_audioFinished = false;
    AVAssetWriter* m_writer = nil;
    AVAssetWriterInput* m_video = nil;
    AVAssetWriterInput* m_audio = nil;
    AVAssetWriterInputPixelBufferAdaptor* m_pixels = nil;
    AVAssetReader* m_reader = nil;
    AVAssetReaderAudioMixOutput* m_decodedAudio = nil;
    CMAudioFormatDescriptionRef m_pcmFormat = nullptr;
};
} // namespace

std::unique_ptr<NativeEditMedia> createNativeEditMedia() { return std::make_unique<AVEditMedia>(); }

bool nativeHevcExportAvailable()
{
    // Probe the actual encoder, including software implementations. Cached:
    // opening the export panel must not create compression sessions repeatedly.
    static const bool available = [] {
        VTCompressionSessionRef session = nullptr;
        const OSStatus result = VTCompressionSessionCreate(kCFAllocatorDefault, 64, 64, kCMVideoCodecType_HEVC,
            nullptr, nullptr, nullptr, nullptr, nullptr, &session);
        if (session) { VTCompressionSessionInvalidate(session); CFRelease(session); }
        return result == noErr;
    }();
    return available;
}
} // namespace SnapTray
