#include "VideoTranscoderTestAudio.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <cmath>

namespace {

constexpr int kDecodeChannels = 2;
constexpr int kDecodeBitDepth = 16;
constexpr float kInt16Scale = 32768.0f;
// Bound placement so a corrupt timestamp cannot request a huge allocation.
constexpr qint64 kMaxDecodedFrames = qint64(60) * 48000;

} // namespace

bool decodeAudioTrack(const QString& path, int sampleRate, DecodedAudio* out, QString* error)
{
    const auto failWith = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];
        if (!track) {
            return failWith(QStringLiteral("No audio track"));
        }
        NSError* nsError = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&nsError];
        if (!reader) {
            return failWith(QStringLiteral("Cannot create audio reader"));
        }
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:@{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVSampleRateKey: @(sampleRate),
            AVNumberOfChannelsKey: @(kDecodeChannels),
            AVLinearPCMBitDepthKey: @(kDecodeBitDepth),
            AVLinearPCMIsFloatKey: @NO,
            AVLinearPCMIsBigEndianKey: @NO,
            AVLinearPCMIsNonInterleaved: @NO
        }];
        if (![reader canAddOutput:output]) {
            return failWith(QStringLiteral("Cannot add PCM output"));
        }
        [reader addOutput:output];
        if (![reader startReading]) {
            return failWith(QStringLiteral("Cannot start audio reader"));
        }

        DecodedAudio decoded;
        decoded.sampleRate = sampleRate;
        bool haveFirst = false;
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            const CMItemCount frames = CMSampleBufferGetNumSamples(sample);
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
            const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
            if (frames <= 0 || !block || !CMTIME_IS_NUMERIC(pts)) {
                CFRelease(sample);
                continue;
            }
            const qint64 frameTime = std::llround(CMTimeGetSeconds(pts) * sampleRate);
            if (!haveFirst) {
                decoded.firstFrame = frameTime;
                haveFirst = true;
            }
            const qint64 offset = frameTime - decoded.firstFrame;
            const size_t bytes = static_cast<size_t>(frames) * kDecodeChannels * sizeof(int16_t);
            std::vector<int16_t> pcm(static_cast<size_t>(frames) * kDecodeChannels);
            const bool copied = CMBlockBufferGetDataLength(block) >= bytes
                && CMBlockBufferCopyDataBytes(block, 0, bytes, pcm.data()) == kCMBlockBufferNoErr;
            CFRelease(sample);
            if (!copied || offset < 0 || offset + frames > kMaxDecodedFrames) {
                return failWith(QStringLiteral("Cannot copy decoded audio"));
            }
            if (decoded.mono.size() < static_cast<size_t>(offset + frames)) {
                decoded.mono.resize(static_cast<size_t>(offset + frames), 0.0f);
            }
            for (CMItemCount i = 0; i < frames; ++i) {
                decoded.mono[static_cast<size_t>(offset + i)] = pcm[static_cast<size_t>(i) * kDecodeChannels] / kInt16Scale;
            }
        }
        if (reader.status != AVAssetReaderStatusCompleted) {
            return failWith(QStringLiteral("Audio decode failed"));
        }
        if (decoded.mono.empty()) {
            return failWith(QStringLiteral("Audio track decoded to no samples"));
        }
        *out = std::move(decoded);
    }
    return true;
}
