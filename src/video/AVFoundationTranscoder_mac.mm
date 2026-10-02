#include "video/IVideoTranscoder.h"

#include "VideoTranscoderFaultInjection.h"
#include "encoding/VideoBitrate.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include <sys/stat.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#if !__has_feature(objc_arc)
#error "AVFoundationTranscoder_mac.mm must be compiled with ARC"
#endif

namespace {

constexpr int kBytesPerPixel = 4;
constexpr int32_t kMsTimescale = 1000;
constexpr double kMsPerSecond = 1000.0;
constexpr int kKeyFrameIntervalSeconds = 2;
constexpr int kDefaultFrameRate = 30;
constexpr int kPercentScale = 100;
constexpr int kMaxProgressBeforeFinish = 99;
constexpr int kCompletePercent = 100;
constexpr int64_t kStatusPollIntervalMs = 20;
// H.264 4:2:0 needs even dimensions; the backend always sends even crops,
// anything else is floored to even rather than handed to the encoder.
constexpr int kEvenAlignmentMask = ~1;
constexpr int kMinEvenSide = 2;
// Edge packets keep the reader's trim attachments, so output audio coverage is
// normally exact apart from the writer's 600-timescale edit-list rounding
// (< 2 ms); kAudioCoverageToleranceMs (IVideoTranscoder.h) also allows a
// source whose edge packets carry no trim, i.e. one whole 1024-sample packet
// per edge (2 x 23.2 ms at 44.1 kHz).

qint64 toMs(CMTime time)
{
    const double seconds = CMTimeGetSeconds(time);
    return std::isfinite(seconds) ? static_cast<qint64>(std::llround(seconds * kMsPerSecond)) : -1;
}

QString cancelledMessage()
{
    return QStringLiteral("Cancelled");
}

// True when both paths name one existing file, including aliases that differ
// only in case on a case-insensitive volume, symlinks and hard links.
bool isSameFile(const QString& a, const QString& b)
{
    struct stat first {};
    struct stat second {};
    return ::stat(QFile::encodeName(a).constData(), &first) == 0
        && ::stat(QFile::encodeName(b).constData(), &second) == 0
        && first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}

NSURL* fileUrl(const QString& path)
{
    return [NSURL fileURLWithPath:path.toNSString()];
}

QString errorText(NSError* error, const QString& fallback)
{
    return error ? QStringLiteral("%1: %2").arg(fallback, QString::fromNSString(error.localizedDescription))
                 : fallback;
}

// Copies `crop` out of a 32BGRA source into a buffer from the writer's pool.
// Returns a +1 buffer, or nullptr on any API failure or out-of-bounds crop.
CVPixelBufferRef copyCroppedPixelBuffer(CVPixelBufferRef source, const QRect& crop, CVPixelBufferPoolRef pool)
{
    if (!source || !pool || CVPixelBufferGetPixelFormatType(source) != kCVPixelFormatType_32BGRA) {
        return nullptr;
    }
    const QRect sourceRect(0, 0, static_cast<int>(CVPixelBufferGetWidth(source)),
                           static_cast<int>(CVPixelBufferGetHeight(source)));
    if (!sourceRect.contains(crop)) {
        return nullptr;
    }
    CVPixelBufferRef target = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &target) != kCVReturnSuccess || !target) {
        return nullptr;
    }
    if (static_cast<int>(CVPixelBufferGetWidth(target)) < crop.width()
        || static_cast<int>(CVPixelBufferGetHeight(target)) < crop.height()) {
        CVPixelBufferRelease(target);
        return nullptr;
    }
    if (CVPixelBufferLockBaseAddress(source, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        CVPixelBufferRelease(target);
        return nullptr;
    }
    if (CVPixelBufferLockBaseAddress(target, 0) != kCVReturnSuccess) {
        CVPixelBufferUnlockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(target);
        return nullptr;
    }
    const auto* src = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(source));
    auto* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(target));
    if (src && dst) {
        const size_t srcStride = CVPixelBufferGetBytesPerRow(source);
        const size_t dstStride = CVPixelBufferGetBytesPerRow(target);
        const size_t rowBytes = static_cast<size_t>(crop.width()) * kBytesPerPixel;
        const size_t xOffset = static_cast<size_t>(crop.x()) * kBytesPerPixel;
        for (int row = 0; row < crop.height(); ++row) {
            std::memcpy(dst + static_cast<size_t>(row) * dstStride,
                        src + static_cast<size_t>(crop.y() + row) * srcStride + xOffset, rowBytes);
        }
    }
    CVPixelBufferUnlockBaseAddress(target, 0);
    CVPixelBufferUnlockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
    if (!src || !dst) {
        CVPixelBufferRelease(target);
        return nullptr;
    }
    return target;
}

// Shifts every timestamp of `sample` by -offset. The copy keeps the sample's
// attachments, including AAC priming/trim durations. Returns +1 or nullptr.
CMSampleBufferRef copyRetimed(CMSampleBufferRef sample, CMTime offset)
{
    CMItemCount count = 0;
    if (CMSampleBufferGetSampleTimingInfoArray(sample, 0, nullptr, &count) != noErr || count <= 0) {
        return nullptr;
    }
    std::vector<CMSampleTimingInfo> timings(static_cast<size_t>(count));
    if (CMSampleBufferGetSampleTimingInfoArray(sample, count, timings.data(), &count) != noErr) {
        return nullptr;
    }
    for (auto& timing : timings) {
        timing.presentationTimeStamp = CMTimeSubtract(timing.presentationTimeStamp, offset);
        if (CMTIME_IS_VALID(timing.decodeTimeStamp)) {
            timing.decodeTimeStamp = CMTimeSubtract(timing.decodeTimeStamp, offset);
        }
    }
    CMSampleBufferRef retimed = nullptr;
    if (CMSampleBufferCreateCopyWithNewTiming(kCFAllocatorDefault, sample, count, timings.data(), &retimed)
        != noErr) {
        return nullptr;
    }
    return retimed;
}

// Presentation range of the first audio track in ms, or {-1, -1} without one.
std::pair<qint64, qint64> audioRangeMs(AVAsset* asset)
{
    AVAssetTrack* audio = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];
    if (!audio) {
        return {-1, -1};
    }
    const CMTimeRange range = audio.timeRange;
    return {toMs(range.start), toMs(CMTimeRangeGetEnd(range))};
}

// Passthrough (outputSettings nil) audio from AVAssetReader is timestamped on
// the track's *media* timeline: the edit list is turned into trim attachments
// (TrimDurationAtStart/AtEnd) but timestamps are not mapped to presentation
// time. An AAC track that starts with priming has an edit mapping media 2112
// samples (44 ms at 48 kHz) to presentation 0, so its packets are 44 ms late
// unless shifted by (media start - presentation start) of the edit. Returns
// that offset, or invalid for an edit list that a single shift cannot map
// (several media segments, or a rate-scaled segment).
CMTime audioMediaOffset(AVAssetTrack* track)
{
    CMTime offset = kCMTimeZero;
    bool found = false;
    for (AVAssetTrackSegment* segment in track.segments) {
        if (segment.isEmpty) {
            continue;
        }
        const CMTimeMapping mapping = segment.timeMapping;
        if (!CMTIME_IS_NUMERIC(mapping.source.start) || !CMTIME_IS_NUMERIC(mapping.target.start)
            || qAbs(toMs(mapping.source.duration) - toMs(mapping.target.duration)) > 1) {
            return kCMTimeInvalid;
        }
        const CMTime segmentOffset = CMTimeSubtract(mapping.source.start, mapping.target.start);
        if (found && qAbs(toMs(segmentOffset) - toMs(offset)) > 1) {
            return kCMTimeInvalid;
        }
        offset = segmentOffset;
        found = true;
    }
    return offset;
}

// Shared by transcode() and the GCD blocks. Blocks capture the shared_ptr by
// value, so the state outlives transcode() if AVFoundation invokes a block late.
struct TranscodeState {
    std::atomic<bool> stop{false};       // cancelled or failed: blocks stop pulling
    std::atomic<bool> cancelled{false};  // progress callback returned false
    std::atomic<bool> videoDone{false};  // video has left the dispatch group
    std::atomic<bool> audioDone{false};  // audio has left the dispatch group
    std::atomic<int> lastPercent{-1};
    std::atomic<int> audioSampleIndex{0};

    std::mutex errorMutex;
    QString error;

    void failWith(const QString& message)
    {
        {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (error.isEmpty()) {
                error = message;
            }
        }
        stop.store(true);
    }

    QString firstError()
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        return error;
    }
};

// Video pump state, owned by the video block. Only touched on the video queue
// (or by the destructor once every block copy is gone).
struct VideoPump {
    // Latest frame presented at or before the range start; shown from 0.
    CVPixelBufferRef lead = nullptr;
    std::deque<std::pair<CVPixelBufferRef, CMTime>> queued;
    CMTime lastAppended = kCMTimeInvalid;

    ~VideoPump()
    {
        if (lead) {
            CVPixelBufferRelease(lead);
        }
        for (auto& entry : queued) {
            CVPixelBufferRelease(entry.first);
        }
    }

    void queueLeadAtZero()
    {
        if (lead) {
            queued.emplace_back(lead, kCMTimeZero);
            lead = nullptr;
        }
    }
};

void leaveOnce(std::atomic<bool>& done, dispatch_group_t group)
{
    if (!done.exchange(true)) {
        dispatch_group_leave(group);
    }
}

class AVFoundationTranscoder final : public IVideoTranscoder, public VideoTranscoderFaultInjection
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override;
    VideoFileProbe probe(const QString& filePath) override;
    void setFaultForTesting(VideoTranscodeFault fault) override { m_fault.store(fault); }

private:
    std::atomic<VideoTranscodeFault> m_fault{VideoTranscodeFault::None};
};

VideoFileProbe AVFoundationTranscoder::probe(const QString& filePath)
{
    VideoFileProbe result;
    if (!QFile::exists(filePath)) {
        return result;
    }
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(filePath) options:nil];
        AVAssetTrack* video = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!video) {
            return result;
        }
        const CGSize size = CGSizeApplyAffineTransform(video.naturalSize, video.preferredTransform);
        result.videoSize = QSize(qAbs(qRound(size.width)), qAbs(qRound(size.height)));
        result.durationMs = toMs(asset.duration);
        result.hasAudio = [asset tracksWithMediaType:AVMediaTypeAudio].count > 0;
        if (result.hasAudio) {
            const auto [audioStartMs, audioEndMs] = audioRangeMs(asset);
            result.audioStartMs = audioStartMs;
            result.audioEndMs = audioEndMs;
        }
        result.valid = !result.videoSize.isEmpty() && result.durationMs > 0;
    }
    return result;
}

VideoTranscodeResult AVFoundationTranscoder::transcode(const VideoTranscodeRequest& request,
                                                       const ProgressCallback& progress)
{
    VideoTranscodeResult result;
    const auto reject = [&](const QString& message) {
        if (message == cancelledMessage()) {
            qDebug() << "AVFoundationTranscoder:" << message;
        } else {
            qWarning() << "AVFoundationTranscoder:" << message;
        }
        result.success = false;
        result.errorMessage = message;
        return result;
    };
    // Only called once the writer is cancelled, failed or finished (or was
    // never started), so nothing writes the file after it is removed.
    const auto fail = [&](const QString& message) {
        QFile::remove(request.outputPath);
        return reject(message);
    };

    if (request.outputPath.isEmpty()) {
        return reject(QStringLiteral("No output path"));
    }
    // Never remove the source: an output that aliases the input is refused
    // before anything touches the output path.
    if (QFileInfo(request.outputPath).absoluteFilePath() == QFileInfo(request.inputPath).absoluteFilePath()
        || isSameFile(request.outputPath, request.inputPath)) {
        return reject(QStringLiteral("Output path must differ from the input path"));
    }

    const VideoTranscodeFault fault = m_fault.load();
    qint64 sourceAudioCoverageMs = -1;
    QSize outputSize;

    @autoreleasepool {
        if (!QFile::exists(request.inputPath)) {
            return fail(QStringLiteral("Input file does not exist"));
        }
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(request.inputPath) options:nil];
        AVAssetTrack* videoTrack = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!videoTrack) {
            return fail(QStringLiteral("Input has no video track"));
        }
        AVAssetTrack* audioTrack = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];

        // The crop is in displayed coordinates (what the player and probe()
        // report), while the reader decodes the stored, untransformed frames.
        // Map it through the inverse track transform and give the output the
        // same orientation, re-anchored to the cropped frame.
        const CGAffineTransform transform = videoTrack.preferredTransform;
        CGAffineTransform orientation = transform;
        orientation.tx = 0;
        orientation.ty = 0;
        // Only axis-aligned orientations (rotations by 90 degrees, flips) map
        // an even-aligned crop to an even-aligned stored rectangle.
        if (!qFuzzyIsNull(orientation.a * orientation.b) || !qFuzzyIsNull(orientation.c * orientation.d)) {
            return fail(QStringLiteral("Cannot crop a video with a skewed orientation"));
        }
        const CGSize natural = videoTrack.naturalSize;
        const QRect storedRect(0, 0, qRound(natural.width), qRound(natural.height));
        const CGRect displayedBounds = CGRectApplyAffineTransform(CGRectMake(0, 0, natural.width, natural.height),
                                                                  transform);
        const QRect frameRect(0, 0, qRound(displayedBounds.size.width), qRound(displayedBounds.size.height));
        QRect crop = request.cropRect.isEmpty() ? frameRect : request.cropRect.intersected(frameRect);
        crop.setWidth(crop.width() & kEvenAlignmentMask);
        crop.setHeight(crop.height() & kEvenAlignmentMask);
        if (crop.width() < kMinEvenSide || crop.height() < kMinEvenSide) {
            return fail(QStringLiteral("Crop rectangle is outside the video"));
        }
        outputSize = crop.size();
        const CGRect displayedCrop = CGRectMake(displayedBounds.origin.x + crop.x(), displayedBounds.origin.y + crop.y(),
                                                crop.width(), crop.height());
        const CGRect storedCropBounds =
            CGRectApplyAffineTransform(displayedCrop, CGAffineTransformInvert(transform));
        // The area of the stored frame to copy; identical to `crop` for an
        // identity transform (every recording SnapTray makes).
        const QRect storedCrop(qRound(storedCropBounds.origin.x), qRound(storedCropBounds.origin.y),
                               qRound(storedCropBounds.size.width), qRound(storedCropBounds.size.height));
        if (!storedRect.contains(storedCrop)) {
            return fail(QStringLiteral("Crop rectangle is outside the video"));
        }
        const CGRect orientedCropBounds =
            CGRectApplyAffineTransform(CGRectMake(0, 0, storedCrop.width(), storedCrop.height()), orientation);
        const CGAffineTransform outputTransform = CGAffineTransformConcat(
            orientation, CGAffineTransformMakeTranslation(-orientedCropBounds.origin.x, -orientedCropBounds.origin.y));

        const qint64 durationMs = toMs(asset.duration);
        if (durationMs <= 0) {
            return fail(QStringLiteral("Input has no duration"));
        }
        const qint64 startMs = qBound<qint64>(0, request.startMs, durationMs);
        const qint64 endMs = request.endMs < 0 ? durationMs : qBound(startMs, request.endMs, durationMs);
        if (endMs <= startMs) {
            return fail(QStringLiteral("Invalid time range"));
        }
        result.startMs = startMs; // AVAssetWriter keeps per-frame timing: the output starts at the request
        const qint64 spanMs = endMs - startMs;
        const CMTime startTime = CMTimeMake(startMs, kMsTimescale);
        const CMTime endTime = CMTimeMake(endMs, kMsTimescale);

        if (audioTrack) {
            const auto [audioStartMs, audioEndMs] = audioRangeMs(asset);
            sourceAudioCoverageMs = qMax<qint64>(0, qMin(audioEndMs, endMs) - qMax(audioStartMs, startMs));
        }
        // A range the source audio never reaches exports video only, as the
        // source is there: the writer gets no audio input that would see no
        // samples. Mirrors the Media Foundation transcoder.
        const bool copyAudio = audioTrack != nil && sourceAudioCoverageMs > 0;

        QFile::remove(request.outputPath);
        NSError* error = nil;
        AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:fileUrl(request.outputPath)
                                                          fileType:AVFileTypeMPEG4
                                                             error:&error];
        if (!writer) {
            return fail(errorText(error, QStringLiteral("Cannot create writer")));
        }

        const int frameRate = videoTrack.nominalFrameRate > 0 ? qMax(1, qRound(videoTrack.nominalFrameRate))
                                                              : kDefaultFrameRate;
        const int bitrate = request.videoBitrate > 0
            ? request.videoBitrate
            : SnapTray::VideoBitrate::forQuality(crop.size(), frameRate, kDefaultTranscodeQuality);
        // Encoded in stored orientation (storedCrop); outputTransform shows it
        // the way the source was shown.
        AVAssetWriterInput* videoInput = [[AVAssetWriterInput alloc]
            initWithMediaType:AVMediaTypeVideo
               outputSettings:@{
                   AVVideoCodecKey: AVVideoCodecTypeH264,
                   AVVideoWidthKey: @(storedCrop.width()),
                   AVVideoHeightKey: @(storedCrop.height()),
                   AVVideoCompressionPropertiesKey: @{
                       AVVideoAverageBitRateKey: @(bitrate),
                       AVVideoExpectedSourceFrameRateKey: @(frameRate),
                       AVVideoMaxKeyFrameIntervalKey: @(frameRate * kKeyFrameIntervalSeconds),
                       AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                       AVVideoAllowFrameReorderingKey: @NO
                   }
               }];
        videoInput.expectsMediaDataInRealTime = NO;
        videoInput.transform = outputTransform;
        AVAssetWriterInputPixelBufferAdaptor* adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
            initWithAssetWriterInput:videoInput
         sourcePixelBufferAttributes:@{
             (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
             (NSString*)kCVPixelBufferWidthKey: @(storedCrop.width()),
             (NSString*)kCVPixelBufferHeightKey: @(storedCrop.height())
         }];
        if (![writer canAddInput:videoInput]) {
            return fail(QStringLiteral("Cannot configure H.264 output"));
        }
        [writer addInput:videoInput];

        // Shift from reader audio timestamps to the output timeline.
        CMTime audioShift = startTime;
        if (copyAudio) {
            const CMTime mediaOffset = audioMediaOffset(audioTrack);
            if (!CMTIME_IS_NUMERIC(mediaOffset)) {
                return fail(QStringLiteral("Cannot preserve source audio: unsupported audio edit list"));
            }
            audioShift = CMTimeAdd(startTime, mediaOffset);
        }

        AVAssetWriterInput* audioInput = nil;
        if (copyAudio) {
            CMFormatDescriptionRef hint =
                (__bridge CMFormatDescriptionRef)audioTrack.formatDescriptions.firstObject;
            // outputSettings nil = passthrough of the compressed source packets.
            AVAssetWriterInput* candidate = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                                           outputSettings:nil
                                                                         sourceFormatHint:hint];
            candidate.expectsMediaDataInRealTime = NO;
            const bool canAdd = fault != VideoTranscodeFault::AudioInputUnsupported && [writer canAddInput:candidate];
            if (!canAdd) {
                // Never fall back to a silent output.
                return fail(QStringLiteral("Cannot preserve source audio"));
            }
            [writer addInput:candidate];
            audioInput = candidate;
        }

        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) {
            return fail(errorText(error, QStringLiteral("Cannot create reader")));
        }
        reader.timeRange = CMTimeRangeFromTimeToTime(startTime, endTime);
        AVAssetReaderTrackOutput* videoOutput = [[AVAssetReaderTrackOutput alloc]
            initWithTrack:videoTrack
           outputSettings:@{ (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) }];
        videoOutput.alwaysCopiesSampleData = NO;
        if (![reader canAddOutput:videoOutput]) {
            return fail(QStringLiteral("Cannot decode source video"));
        }
        [reader addOutput:videoOutput];
        AVAssetReaderTrackOutput* audioOutput = nil;
        if (audioInput) {
            audioOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:audioTrack outputSettings:nil];
            if (![reader canAddOutput:audioOutput]) {
                return fail(QStringLiteral("Cannot read source audio"));
            }
            [reader addOutput:audioOutput];
        }

        if (![reader startReading]) {
            return fail(errorText(reader.error, QStringLiteral("Cannot start reading")));
        }
        if (![writer startWriting]) {
            [reader cancelReading];
            if (writer.status == AVAssetWriterStatusWriting) {
                [writer cancelWriting];
            }
            return fail(errorText(writer.error, QStringLiteral("Cannot start writing")));
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        auto state = std::make_shared<TranscodeState>();
        auto videoPump = std::make_shared<VideoPump>();
        const ProgressCallback progressCopy = progress;
        const double spanForProgress = static_cast<double>(spanMs);
        dispatch_group_t group = dispatch_group_create();

        // Weak captures: each input retains its block, so strong captures of
        // the input or adaptor would form a retain cycle.
        __weak AVAssetWriterInput* weakVideoInput = videoInput;
        __weak AVAssetWriterInputPixelBufferAdaptor* weakAdaptor = adaptor;
        dispatch_queue_t videoQueue = dispatch_queue_create("snaptray.transcode.video", DISPATCH_QUEUE_SERIAL);
        dispatch_group_enter(group);
        [videoInput requestMediaDataWhenReadyOnQueue:videoQueue usingBlock:^{
            AVAssetWriterInput* input = weakVideoInput;
            AVAssetWriterInputPixelBufferAdaptor* pixelAdaptor = weakAdaptor;
            if (state->videoDone.load()) {
                return;
            }
            while (input.isReadyForMoreMediaData) {
                if (state->stop.load()) {
                    leaveOnce(state->videoDone, group);
                    return;
                }
                @autoreleasepool {
                    if (!videoPump->queued.empty()) {
                        auto [buffer, pts] = videoPump->queued.front();
                        videoPump->queued.pop_front();
                        const bool appended = [pixelAdaptor appendPixelBuffer:buffer withPresentationTime:pts];
                        CVPixelBufferRelease(buffer);
                        if (!appended) {
                            state->failWith(QStringLiteral("Cannot encode video frame"));
                            leaveOnce(state->videoDone, group);
                            return;
                        }
                        videoPump->lastAppended = pts;
                        const int percent = qBound(0,
                            static_cast<int>(CMTimeGetSeconds(pts) * kMsPerSecond * kPercentScale / spanForProgress),
                            kMaxProgressBeforeFinish);
                        if (state->lastPercent.exchange(percent) != percent && progressCopy
                            && !progressCopy(percent)) {
                            state->cancelled.store(true);
                            state->stop.store(true);
                        }
                        continue;
                    }

                    CMSampleBufferRef sample = [videoOutput copyNextSampleBuffer];
                    if (!sample) {
                        // End of range, or a reader failure that transcode() detects.
                        if (videoPump->lead) {
                            videoPump->queueLeadAtZero();
                            continue;
                        }
                        [input markAsFinished];
                        leaveOnce(state->videoDone, group);
                        return;
                    }
                    const CMTime pts = CMTimeSubtract(CMSampleBufferGetPresentationTimeStamp(sample), startTime);
                    CVPixelBufferRef cropped =
                        copyCroppedPixelBuffer(CMSampleBufferGetImageBuffer(sample), storedCrop, pixelAdaptor.pixelBufferPool);
                    CFRelease(sample);
                    if (!cropped) {
                        state->failWith(QStringLiteral("Cannot crop video frame"));
                        leaveOnce(state->videoDone, group);
                        return;
                    }
                    if (!CMTIME_IS_NUMERIC(pts)) {
                        CVPixelBufferRelease(cropped);
                        state->failWith(QStringLiteral("Video frame has an invalid timestamp"));
                        leaveOnce(state->videoDone, group);
                        return;
                    }
                    if (CMTIME_COMPARE_INLINE(pts, <=, kCMTimeZero)) {
                        // The frame on screen at the range start is the newest one at or before it.
                        if (videoPump->lead) {
                            CVPixelBufferRelease(videoPump->lead);
                        }
                        videoPump->lead = cropped;
                        continue;
                    }
                    videoPump->queueLeadAtZero();
                    const CMTime previous = videoPump->queued.empty() ? videoPump->lastAppended
                                                                      : videoPump->queued.back().second;
                    if (CMTIME_IS_VALID(previous) && CMTIME_COMPARE_INLINE(pts, <=, previous)) {
                        CVPixelBufferRelease(cropped);
                        continue;
                    }
                    videoPump->queued.emplace_back(cropped, pts);
                }
            }
        }];

        dispatch_queue_t audioQueue = nullptr;
        if (audioInput) {
            __weak AVAssetWriterInput* weakAudioInput = audioInput;
            audioQueue = dispatch_queue_create("snaptray.transcode.audio", DISPATCH_QUEUE_SERIAL);
            dispatch_group_enter(group);
            [audioInput requestMediaDataWhenReadyOnQueue:audioQueue usingBlock:^{
                AVAssetWriterInput* input = weakAudioInput;
                if (state->audioDone.load()) {
                    return;
                }
                while (input.isReadyForMoreMediaData) {
                    if (state->stop.load()) {
                        leaveOnce(state->audioDone, group);
                        return;
                    }
                    CMSampleBufferRef sample = [audioOutput copyNextSampleBuffer];
                    if (!sample) {
                        [input markAsFinished];
                        leaveOnce(state->audioDone, group);
                        return;
                    }
                    if (CMSampleBufferGetNumSamples(sample) == 0) {
                        // Data-less marker (e.g. DrainAfterDecoding) with no
                        // timing; it means nothing to a passthrough writer.
                        CFRelease(sample);
                        continue;
                    }
                    const bool faultHere = state->audioSampleIndex.fetch_add(1) == kVideoTranscodeFaultAudioSampleIndex;
                    // The reader already cuts the range at packet granularity
                    // and marks the part of a straddling edge packet outside
                    // the range with a trim attachment. Edge packets are kept
                    // (start may become negative) with that trim, so the
                    // writer's edit list starts the audio exactly at 0.
                    CMSampleBufferRef retimed = (faultHere && fault == VideoTranscodeFault::AudioRetimeFailure)
                        ? nullptr
                        : copyRetimed(sample, audioShift);
                    CFRelease(sample);
                    if (!retimed) {
                        state->failWith(QStringLiteral("Cannot retime source audio"));
                        leaveOnce(state->audioDone, group);
                        return;
                    }
                    const bool appended = !(faultHere && fault == VideoTranscodeFault::AudioAppendFailure)
                        && [input appendSampleBuffer:retimed];
                    CFRelease(retimed);
                    if (!appended) {
                        state->failWith(QStringLiteral("Cannot write source audio"));
                        leaveOnce(state->audioDone, group);
                        return;
                    }
                }
            }];
        }

        // Poll so a stop or a failed reader/writer is noticed even when an
        // input's block is never invoked again.
        bool drained = false;
        while (!drained) {
            drained = dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW,
                          kStatusPollIntervalMs * static_cast<int64_t>(NSEC_PER_MSEC))) == 0;
            if (!drained && (state->stop.load() || writer.status == AVAssetWriterStatusFailed
                             || reader.status == AVAssetReaderStatusFailed)) {
                break;
            }
        }

        const bool aborted = !drained || state->stop.load() || writer.status != AVAssetWriterStatusWriting
            || reader.status != AVAssetReaderStatusCompleted;
        if (aborted) {
            state->stop.store(true);
            if (reader.status == AVAssetReaderStatusReading) {
                [reader cancelReading];
            }
            if (writer.status == AVAssetWriterStatusWriting) {
                [writer cancelWriting];
            }
            // Wait out any block still running so no progress callback or
            // append happens after this function returns, then balance the
            // group for inputs whose block will never be invoked again.
            dispatch_sync(videoQueue, ^{});
            if (audioQueue) {
                dispatch_sync(audioQueue, ^{});
            }
            leaveOnce(state->videoDone, group);
            if (audioQueue) {
                leaveOnce(state->audioDone, group);
            }

            QString message = state->firstError();
            if (!message.isEmpty()) {
                // A failed append usually leaves the reason on the writer.
                message = errorText(writer.status == AVAssetWriterStatusFailed ? writer.error : nil, message);
            } else if (writer.status == AVAssetWriterStatusFailed) {
                message = errorText(writer.error, QStringLiteral("Video writer failed"));
            } else if (reader.status == AVAssetReaderStatusFailed) {
                message = errorText(reader.error, QStringLiteral("Cannot read source"));
            }
            if (message.isEmpty()) {
                message = state->cancelled.load() ? cancelledMessage()
                                                  : QStringLiteral("Transcode did not complete");
            }
            return fail(message);
        }

        [writer endSessionAtSourceTime:CMTimeSubtract(endTime, startTime)];
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(finished);
        }];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
        if (writer.status != AVAssetWriterStatusCompleted) {
            return fail(errorText(writer.error, QStringLiteral("Cannot finish writing")));
        }
    }

    // Validate the finished file before reporting success; the caller deletes
    // the source only on success.
    const VideoFileProbe outputProbe = probe(request.outputPath);
    if (!outputProbe.valid || outputProbe.videoSize != outputSize) {
        return fail(QStringLiteral("Output validation failed; source retained"));
    }
    qint64 outputAudioMs = -1;
    @autoreleasepool {
        const auto [audioStartMs, audioEndMs] =
            audioRangeMs([AVURLAsset URLAssetWithURL:fileUrl(request.outputPath) options:nil]);
        outputAudioMs = audioStartMs < 0 ? -1 : audioEndMs - audioStartMs;
    }
    // A selection the source audio does not reach (coverage within the packet
    // tolerance) has nothing to preserve; any other gap is a silent downgrade.
    if (sourceAudioCoverageMs > kAudioCoverageToleranceMs
        && (outputAudioMs <= 0 || outputAudioMs < sourceAudioCoverageMs - kAudioCoverageToleranceMs)) {
        return fail(QStringLiteral("Output is missing source audio; source retained"));
    }

    // Completion is reported only for a validated output. Returning false
    // here still cancels: the caller asked to stop, so nothing is kept.
    if (progress && !progress(kCompletePercent)) {
        return fail(cancelledMessage());
    }
    result.success = true;
    result.audioCopied = outputAudioMs > 0;
    return result;
}

} // namespace

std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder()
{
    return std::make_unique<AVFoundationTranscoder>();
}
