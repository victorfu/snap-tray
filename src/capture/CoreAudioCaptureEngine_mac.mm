#include "capture/CoreAudioCaptureEngine.h"

#ifdef Q_OS_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>

#include <QDebug>
#include <QPointer>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <chrono>

#if !__has_feature(objc_arc)
#error "CoreAudioCaptureEngine_mac.mm requires Objective-C ARC"
#endif

static char kCaptureQueueSpecificKey;

// Check if ScreenCaptureKit is available (macOS 12.3+)
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120300
#define HAS_SCREENCAPTUREKIT 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

#if HAS_SCREENCAPTUREKIT
struct SystemAudioFailureBridge
{
    QMutex mutex;
    QPointer<CoreAudioCaptureEngine> engine;
};
#endif

// Objective-C delegate for audio capture
@interface AudioCaptureDelegate : NSObject <AVCaptureAudioDataOutputSampleBufferDelegate>
@property (atomic, assign) CoreAudioCaptureEngine *engine;
@property (atomic, assign) BOOL invalidated;
@property (nonatomic, assign) qint64 startTime;
@property (nonatomic, assign) qint64 pausedDuration;
@property (nonatomic, assign) bool paused;
@property (nonatomic, assign) BOOL hasTimelineAnchor;
@property (nonatomic, assign) qint64 firstPresentationTimeNs;
@property (nonatomic, assign) qint64 firstTimelineTimeNs;
@end

static QByteArray convertAudioBufferToPCM16(CMSampleBufferRef sampleBuffer,
                                             const char *label,
                                             bool *formatLogged,
                                             SnapTray::Audio::Pcm16Format *outputFormat,
                                             bool *unsupportedFormat)
{
    if (unsupportedFormat) {
        *unsupportedFormat = false;
    }
    CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (!asbd) return QByteArray();

    CMItemCount numFrames = CMSampleBufferGetNumSamples(sampleBuffer);
    if (numFrames <= 0) return QByteArray();

    int channels = static_cast<int>(asbd->mChannelsPerFrame);
    if (channels <= 0) return QByteArray();

    if (outputFormat) {
        outputFormat->sampleRate = qRound(asbd->mSampleRate);
        outputFormat->channels = channels;
        outputFormat->byteOrder = SnapTray::Audio::ByteOrder::LittleEndian;
        outputFormat->signedSamples = true;
        outputFormat->interleaved = true;
    }

    bool isFloat = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    bool isSignedInteger = (asbd->mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
    bool isBigEndian = (asbd->mFormatFlags & kAudioFormatFlagIsBigEndian) != 0;
    bool isNonInterleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    int bitsPerChannel = static_cast<int>(asbd->mBitsPerChannel);

    if (formatLogged && !*formatLogged) {
        qDebug() << "CoreAudioCaptureEngine:" << label << "audio format -"
                 << asbd->mSampleRate << "Hz,"
                 << channels << "ch,"
                 << bitsPerChannel << "bit"
                 << (isFloat ? "(float)" : "(int)")
                 << (isNonInterleaved ? "(non-interleaved)" : "(interleaved)");
        *formatLogged = true;
    }

    int bytesPerSample = bitsPerChannel / 8;
    if (bytesPerSample <= 0) return QByteArray();

    size_t bufferListSize = offsetof(AudioBufferList, mBuffers)
        + sizeof(AudioBuffer) * static_cast<size_t>(qMax(1, channels));
    AudioBufferList *bufferList = reinterpret_cast<AudioBufferList *>(malloc(bufferListSize));
    if (!bufferList) return QByteArray();
    bufferList->mNumberBuffers = 0;

    CMBlockBufferRef blockBuffer = nullptr;
    OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
        sampleBuffer,
        nullptr,
        bufferList,
        bufferListSize,
        nullptr,
        nullptr,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
        &blockBuffer
    );

    if (status != noErr || bufferList->mNumberBuffers == 0) {
        if (blockBuffer) {
            CFRelease(blockBuffer);
        }
        free(bufferList);
        return QByteArray();
    }

    int frameCount = static_cast<int>(numFrames);
    QByteArray output(frameCount * channels * static_cast<int>(sizeof(int16_t)), 0);
    int16_t *outPtr = reinterpret_cast<int16_t *>(output.data());

    if (isFloat && bitsPerChannel == 32) {
        if (isNonInterleaved && bufferList->mNumberBuffers >= static_cast<UInt32>(channels)) {
            int minFrames = frameCount;
            for (int ch = 0; ch < channels; ch++) {
                const AudioBuffer &buf = bufferList->mBuffers[ch];
                int available = static_cast<int>(buf.mDataByteSize / sizeof(float));
                minFrames = qMin(minFrames, available);
            }
            for (int i = 0; i < minFrames; i++) {
                for (int ch = 0; ch < channels; ch++) {
                    const float *inPtr = reinterpret_cast<const float *>(bufferList->mBuffers[ch].mData);
                    float sample = inPtr[i];
                    if (sample > 1.0f) sample = 1.0f;
                    else if (sample < -1.0f) sample = -1.0f;
                    outPtr[i * channels + ch] = static_cast<int16_t>(sample * 32767.0f);
                }
            }
        } else {
            const AudioBuffer &buf = bufferList->mBuffers[0];
            const float *inPtr = reinterpret_cast<const float *>(buf.mData);
            int totalSamples = frameCount * channels;
            int available = static_cast<int>(buf.mDataByteSize / sizeof(float));
            int sampleCount = qMin(totalSamples, available);
            for (int i = 0; i < sampleCount; i++) {
                float sample = inPtr[i];
                if (sample > 1.0f) sample = 1.0f;
                else if (sample < -1.0f) sample = -1.0f;
                outPtr[i] = static_cast<int16_t>(sample * 32767.0f);
            }
        }
    } else if (!isFloat && isSignedInteger && !isBigEndian && bitsPerChannel == 16) {
        if (isNonInterleaved && bufferList->mNumberBuffers >= static_cast<UInt32>(channels)) {
            int minFrames = frameCount;
            for (int ch = 0; ch < channels; ch++) {
                const AudioBuffer &buf = bufferList->mBuffers[ch];
                int available = static_cast<int>(buf.mDataByteSize / sizeof(int16_t));
                minFrames = qMin(minFrames, available);
            }
            for (int i = 0; i < minFrames; i++) {
                for (int ch = 0; ch < channels; ch++) {
                    const int16_t *inPtr = reinterpret_cast<const int16_t *>(bufferList->mBuffers[ch].mData);
                    outPtr[i * channels + ch] = inPtr[i];
                }
            }
        } else {
            const AudioBuffer &buf = bufferList->mBuffers[0];
            int totalBytes = frameCount * channels * static_cast<int>(sizeof(int16_t));
            int copyBytes = qMin(totalBytes, static_cast<int>(buf.mDataByteSize));
            memcpy(outPtr, buf.mData, static_cast<size_t>(copyBytes));
        }
    } else {
        qWarning() << "CoreAudioCaptureEngine:" << label << "unsupported audio format";
        if (unsupportedFormat) {
            *unsupportedFormat = true;
        }
        output.clear();
    }

    if (blockBuffer) {
        CFRelease(blockBuffer);
    }
    free(bufferList);
    return output;
}

static QByteArray convertMicAudioBufferToPCM16(
    CMSampleBufferRef sampleBuffer,
    SnapTray::Audio::Pcm16Format *format,
    bool *unsupportedFormat)
{
    static bool micFormatLogged = false;
    return convertAudioBufferToPCM16(
        sampleBuffer, "Mic", &micFormatLogged, format, unsupportedFormat);
}

static qint64 alignedSampleTimestampNs(CMSampleBufferRef sampleBuffer,
                                       BOOL *hasAnchor,
                                       qint64 *firstPresentationTimeNs,
                                       qint64 *firstTimelineTimeNs,
                                       qint64 fallbackTimelineNs)
{
    const CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    if (!CMTIME_IS_VALID(presentationTime) || presentationTime.timescale <= 0) {
        return qMax<qint64>(qint64(0), fallbackTimelineNs);
    }

    const CMTime nanoseconds = CMTimeConvertScale(
        presentationTime,
        static_cast<int32_t>(NSEC_PER_SEC),
        kCMTimeRoundingMethod_RoundHalfAwayFromZero);
    if (!CMTIME_IS_VALID(nanoseconds)) {
        return qMax<qint64>(qint64(0), fallbackTimelineNs);
    }

    if (!*hasAnchor) {
        *hasAnchor = YES;
        *firstPresentationTimeNs = nanoseconds.value;
        *firstTimelineTimeNs = qMax<qint64>(qint64(0), fallbackTimelineNs);
    }
    return qMax<qint64>(
        qint64(0),
        *firstTimelineTimeNs + nanoseconds.value - *firstPresentationTimeNs);
}

@implementation AudioCaptureDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection
{
    CoreAudioCaptureEngine *engine = self.engine;
    if (self.invalidated || self.paused || !engine) return;

    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    SnapTray::Audio::Pcm16Format format;
    bool unsupportedFormat = false;
    QByteArray audioData = convertMicAudioBufferToPCM16(
        sampleBuffer, &format, &unsupportedFormat);
    if (audioData.isEmpty()) {
        if (unsupportedFormat && CMSampleBufferGetNumSamples(sampleBuffer) > 0) {
            const QPointer<CoreAudioCaptureEngine> engineGuard(engine);
            if (engineGuard) {
                engineGuard->reportCapturedAudioFormatFailure(
                    SnapTray::Audio::Source::Microphone,
                    qMax<qint64>(
                        qint64(0),
                        (now - self.startTime - self.pausedDuration)
                            * NSEC_PER_MSEC));
            }
        }
        return;
    }

    const qint64 chunkFrames = audioData.size() / qMax(1, format.channels * 2);
    const qint64 chunkDurationNs = format.sampleRate > 0
        ? chunkFrames * NSEC_PER_SEC / format.sampleRate
        : 0;
    const qint64 fallbackTimestampNs = qMax<qint64>(
        qint64(0),
        (now - self.startTime - self.pausedDuration) * NSEC_PER_MSEC
            - chunkDurationNs);
    BOOL hasAnchor = self.hasTimelineAnchor;
    qint64 firstPresentationTimeNs = self.firstPresentationTimeNs;
    qint64 firstTimelineTimeNs = self.firstTimelineTimeNs;
    const qint64 timestampNs = alignedSampleTimestampNs(
        sampleBuffer,
        &hasAnchor,
        &firstPresentationTimeNs,
        &firstTimelineTimeNs,
        fallbackTimestampNs);
    self.hasTimelineAnchor = hasAnchor;
    self.firstPresentationTimeNs = firstPresentationTimeNs;
    self.firstTimelineTimeNs = firstTimelineTimeNs;

    const QPointer<CoreAudioCaptureEngine> engineGuard(engine);
    if (engineGuard) {
        engineGuard->processCapturedAudio(
            SnapTray::Audio::Source::Microphone,
            audioData,
            format,
            timestampNs);
    }
}

@end

#if HAS_SCREENCAPTUREKIT
// ScreenCaptureKit delegate for system audio (macOS 13+)
API_AVAILABLE(macos(13.0))
@interface SCKAudioCaptureDelegate : NSObject <SCStreamDelegate, SCStreamOutput>
{
@public
    std::shared_ptr<SystemAudioFailureBridge> failureBridge;
}
@property (atomic, assign) CoreAudioCaptureEngine *engine;
@property (atomic, assign) BOOL invalidated;
@property (nonatomic, assign) qint64 startTime;
@property (nonatomic, assign) qint64 pausedDuration;
@property (nonatomic, assign) bool paused;
@property (nonatomic, assign) BOOL hasTimelineAnchor;
@property (nonatomic, assign) qint64 firstPresentationTimeNs;
@property (nonatomic, assign) qint64 firstTimelineTimeNs;
@end

static QByteArray convertSckAudioBufferToPCM16(
    CMSampleBufferRef sampleBuffer,
    SnapTray::Audio::Pcm16Format *format,
    bool *unsupportedFormat)
{
    static bool sckFormatLogged = false;
    return convertAudioBufferToPCM16(
        sampleBuffer, "SCK", &sckFormatLogged, format, unsupportedFormat);
}

API_AVAILABLE(macos(13.0))
@implementation SCKAudioCaptureDelegate

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio) return;
    CoreAudioCaptureEngine *engine = self.engine;
    if (self.invalidated || self.paused || !engine) return;

    const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    SnapTray::Audio::Pcm16Format format;
    bool unsupportedFormat = false;
    QByteArray audioData = convertSckAudioBufferToPCM16(
        sampleBuffer, &format, &unsupportedFormat);
    if (audioData.isEmpty()) {
        if (unsupportedFormat && CMSampleBufferGetNumSamples(sampleBuffer) > 0) {
            const QPointer<CoreAudioCaptureEngine> engineGuard(engine);
            if (engineGuard) {
                engineGuard->reportCapturedAudioFormatFailure(
                    SnapTray::Audio::Source::SystemAudio,
                    qMax<qint64>(
                        qint64(0),
                        (now - self.startTime - self.pausedDuration)
                            * NSEC_PER_MSEC));
            }
        }
        return;
    }

    const qint64 chunkFrames = audioData.size() / qMax(1, format.channels * 2);
    const qint64 chunkDurationNs = format.sampleRate > 0
        ? chunkFrames * NSEC_PER_SEC / format.sampleRate
        : 0;
    const qint64 fallbackTimestampNs = qMax<qint64>(
        qint64(0),
        (now - self.startTime - self.pausedDuration) * NSEC_PER_MSEC
            - chunkDurationNs);
    BOOL hasAnchor = self.hasTimelineAnchor;
    qint64 firstPresentationTimeNs = self.firstPresentationTimeNs;
    qint64 firstTimelineTimeNs = self.firstTimelineTimeNs;
    const qint64 timestampNs = alignedSampleTimestampNs(
        sampleBuffer,
        &hasAnchor,
        &firstPresentationTimeNs,
        &firstTimelineTimeNs,
        fallbackTimestampNs);
    self.hasTimelineAnchor = hasAnchor;
    self.firstPresentationTimeNs = firstPresentationTimeNs;
    self.firstTimelineTimeNs = firstTimelineTimeNs;

    const QPointer<CoreAudioCaptureEngine> engineGuard(engine);
    if (engineGuard) {
        engineGuard->processCapturedAudio(
            SnapTray::Audio::Source::SystemAudio,
            audioData,
            format,
            timestampNs);
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    if (!error || self.invalidated) {
        return;
    }

    const QString details = QString::fromNSString(error.localizedDescription);
    qWarning() << "CoreAudioCaptureEngine: System audio stream stopped:" << details;

    // Stream-delegate callbacks are not guaranteed to run on captureQueue.
    // Serialize scheduling with teardown, then re-check the guarded target on
    // its QObject thread before reporting the runtime source loss.
    const auto bridge = failureBridge;
    if (!bridge) {
        return;
    }

    QMutexLocker locker(&bridge->mutex);
    CoreAudioCaptureEngine *target = bridge->engine.data();
    if (!target) {
        return;
    }
    QMetaObject::invokeMethod(target, [bridge, details]() {
        QPointer<CoreAudioCaptureEngine> guardedTarget;
        {
            QMutexLocker bridgeLocker(&bridge->mutex);
            guardedTarget = bridge->engine;
        }
        if (guardedTarget) {
            guardedTarget->handleSystemAudioCaptureFailure(details);
        }
    }, Qt::QueuedConnection);
}

@end
#endif

// Private implementation
class CoreAudioCaptureEngine::Private
{
public:
    AVCaptureSession *captureSession = nil;
    AVCaptureDeviceInput *audioInput = nil;
    AVCaptureAudioDataOutput *audioOutput = nil;
    AudioCaptureDelegate *audioDelegate = nil;
    dispatch_queue_t captureQueue = nil;

#if HAS_SCREENCAPTUREKIT
    SCStream *scStream API_AVAILABLE(macos(13.0)) = nil;
    SCKAudioCaptureDelegate *scDelegate API_AVAILABLE(macos(13.0)) = nil;
    std::shared_ptr<SystemAudioFailureBridge> systemAudioFailureBridge;
    bool scOutputAdded = false;
    bool scCaptureStarted = false;
#endif

    ~Private() {
        cleanup();
    }

    void drainCaptureQueue() {
        if (captureQueue &&
            dispatch_get_specific(&kCaptureQueueSpecificKey) != this) {
            dispatch_sync(captureQueue, ^{});
        }
    }

    void updateDelegatePauseState(bool paused,
                                  qint64 pausedDuration,
                                  bool resetTimelineAnchor) {
        auto update = ^{
            if (audioDelegate) {
                audioDelegate.pausedDuration = pausedDuration;
                if (resetTimelineAnchor) {
                    audioDelegate.hasTimelineAnchor = NO;
                }
                audioDelegate.paused = paused;
            }
#if HAS_SCREENCAPTUREKIT
            if (@available(macOS 13.0, *)) {
                if (scDelegate) {
                    scDelegate.pausedDuration = pausedDuration;
                    if (resetTimelineAnchor) {
                        scDelegate.hasTimelineAnchor = NO;
                    }
                    scDelegate.paused = paused;
                }
            }
#endif
        };

        if (captureQueue && dispatch_get_specific(&kCaptureQueueSpecificKey) != this) {
            dispatch_sync(captureQueue, update);
        } else {
            update();
        }
    }

#if HAS_SCREENCAPTUREKIT
    void cleanupSystemAudio() {
        if (@available(macOS 13.0, *)) {
            // Keep the Objective-C graph alive until ScreenCaptureKit confirms
            // its asynchronous stop, even after the C++ owner releases d.
            SCStream *localStream = scStream;
            SCKAudioCaptureDelegate *localDelegate = scDelegate;
            const bool outputAdded = scOutputAdded;
            const bool captureStarted = scCaptureStarted;

            if (localDelegate) {
                localDelegate.invalidated = YES;
                localDelegate.engine = nil;
            }

            if (systemAudioFailureBridge) {
                QMutexLocker locker(&systemAudioFailureBridge->mutex);
                systemAudioFailureBridge->engine = nullptr;
            }

            if (localStream && localDelegate && outputAdded) {
                NSError *removeError = nil;
                if (![localStream removeStreamOutput:localDelegate
                                                type:SCStreamOutputTypeAudio
                                               error:&removeError]) {
                    qWarning() << "CoreAudioCaptureEngine: Failed to remove system audio output:"
                               << (removeError
                                       ? QString::fromNSString(removeError.localizedDescription)
                                       : QStringLiteral("unknown error"));
                }
            }
            scOutputAdded = false;

            if (localStream && captureStarted) {
                [localStream stopCaptureWithCompletionHandler:^(NSError *error) {
                    // Capturing both objects retains the asynchronous callback
                    // graph. This block intentionally never captures d or the
                    // CoreAudioCaptureEngine instance.
                    (void)localStream;
                    (void)localDelegate;
                    if (error) {
                        qWarning() << "CoreAudioCaptureEngine: Failed to stop system audio:"
                                   << QString::fromNSString(error.localizedDescription);
                    }
                }];
            }
            scCaptureStarted = false;

            // A callback may already have copied the old raw engine target.
            // Drain it before the C++ owner can continue destruction. Any
            // callback scheduled later observes invalidated/engine == nil.
            drainCaptureQueue();
            scStream = nil;
            scDelegate = nil;
            systemAudioFailureBridge.reset();
        } else {
            // HAS_SCREENCAPTUREKIT reflects the build SDK. On macOS 12 the
            // microphone still uses this queue even though SCK audio is not
            // available at runtime.
            drainCaptureQueue();
            if (systemAudioFailureBridge) {
                {
                    QMutexLocker locker(&systemAudioFailureBridge->mutex);
                    systemAudioFailureBridge->engine = nullptr;
                }
                systemAudioFailureBridge.reset();
            }
        }
    }
#endif

    void cleanupMicrophone() {
        if (audioDelegate) {
            audioDelegate.invalidated = YES;
            audioDelegate.engine = nil;
        }
        if (audioOutput) {
            [audioOutput setSampleBufferDelegate:nil queue:nil];
        }
        if (captureSession) {
            [captureSession stopRunning];
        }
        drainCaptureQueue();
        captureSession = nil;
        audioInput = nil;
        audioOutput = nil;
        audioDelegate = nil;
    }

    void cleanup() {
        cleanupMicrophone();

#if HAS_SCREENCAPTUREKIT
        cleanupSystemAudio();
#else
        drainCaptureQueue();
#endif

        captureQueue = nil;
    }
};

CoreAudioCaptureEngine::CoreAudioCaptureEngine(QObject *parent)
    : IAudioCaptureEngine(parent)
    , d(new Private)
{
    m_format = SnapTray::Audio::TimestampedPcmMixer::outputFormat();
}

CoreAudioCaptureEngine::~CoreAudioCaptureEngine()
{
    stop();
    delete d;
}

bool CoreAudioCaptureEngine::isAvailable() const
{
    // System audio via ScreenCaptureKit doesn't require microphone permission
    if (isSystemAudioSupported()) {
        return true;
    }

    // For microphone capture, check permission
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    return status == AVAuthorizationStatusAuthorized || status == AVAuthorizationStatusNotDetermined;
}

bool CoreAudioCaptureEngine::isSystemAudioSupported() const
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        return true;
    }
#endif
    return false;
}

QString CoreAudioCaptureEngine::engineName() const
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        return QStringLiteral("CoreAudio+ScreenCaptureKit");
    }
#endif
    return QStringLiteral("CoreAudio");
}

bool CoreAudioCaptureEngine::setAudioSource(AudioSource source)
{
    if (m_running) {
        qDebug() << "CoreAudioCaptureEngine: Cannot change source while running";
        return false;
    }

    // Check if system audio is requested but not supported
    if ((source == AudioSource::SystemAudio || source == AudioSource::Both) &&
        !isSystemAudioSupported()) {
        qWarning() << "CoreAudioCaptureEngine: System audio not supported on this macOS version";
        if (source == AudioSource::SystemAudio) {
            emit error("System audio capture requires macOS 13 or later");
            return false;
        }
    }

    m_source = source;
    return true;
}

bool CoreAudioCaptureEngine::setDevice(const QString &deviceId)
{
    if (m_running) {
        qDebug() << "CoreAudioCaptureEngine: Cannot change device while running";
        return false;
    }
    m_deviceId = deviceId;
    return true;
}

QList<IAudioCaptureEngine::AudioDevice> CoreAudioCaptureEngine::availableInputDevices() const
{
    QList<AudioDevice> devices;

    NSArray<AVCaptureDeviceType> *deviceTypes;
    if (@available(macOS 14.0, *)) {
        deviceTypes = @[AVCaptureDeviceTypeMicrophone];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        deviceTypes = @[AVCaptureDeviceTypeBuiltInMicrophone, AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    }
    AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession
        discoverySessionWithDeviceTypes:deviceTypes
        mediaType:AVMediaTypeAudio
        position:AVCaptureDevicePositionUnspecified];
    NSArray<AVCaptureDevice *> *audioDevices = discoverySession.devices;
    AVCaptureDevice *defaultDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];

    for (AVCaptureDevice *device in audioDevices) {
        AudioDevice info;
        info.id = QString::fromNSString(device.uniqueID);
        info.name = QString::fromNSString(device.localizedName);
        info.isDefault = (defaultDevice && [device.uniqueID isEqualToString:defaultDevice.uniqueID]);
        devices.append(info);
    }

    return devices;
}

QString CoreAudioCaptureEngine::defaultInputDevice() const
{
    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    if (device) {
        return QString::fromNSString(device.uniqueID);
    }
    return QString();
}

IAudioCaptureEngine::AudioSource CoreAudioCaptureEngine::activeAudioSource() const
{
    const bool microphone = m_microphoneActive.load();
    const bool systemAudio = m_systemAudioActive.load();
    if (microphone && systemAudio) {
        return AudioSource::Both;
    }
    if (microphone) {
        return AudioSource::Microphone;
    }
    if (systemAudio) {
        return AudioSource::SystemAudio;
    }
    return AudioSource::None;
}

void CoreAudioCaptureEngine::notifyActiveSourceChanged()
{
    if (m_starting.load() || m_stopping.load()) {
        return;
    }
    const AudioSource activeSource = activeAudioSource();
    const int activeValue = static_cast<int>(activeSource);
    if (m_lastNotifiedActiveSource.exchange(activeValue) != activeValue) {
        emit activeSourceChanged(activeSource);
    }
}

bool CoreAudioCaptureEngine::start()
{
    if (m_running) {
        qDebug() << "CoreAudioCaptureEngine: Already running";
        return false;
    }

    if (m_source == AudioSource::None) {
        qWarning() << "CoreAudioCaptureEngine: No audio source configured";
        return false;
    }

    struct StartFlagGuard
    {
        std::atomic<bool>& flag;
        ~StartFlagGuard() { flag = false; }
    };
    m_starting = true;
    const StartFlagGuard startFlagGuard{m_starting};

    const bool wantsMicrophone = m_source == AudioSource::Microphone
        || m_source == AudioSource::Both;
    const bool wantsSystemAudio = m_source == AudioSource::SystemAudio
        || m_source == AudioSource::Both;
    SnapTray::Audio::TimestampedPcmMixer::Config mixerConfig;
    mixerConfig.microphoneEnabled = wantsMicrophone;
    mixerConfig.systemAudioEnabled = wantsSystemAudio && isSystemAudioSupported();
    m_mixer = std::make_unique<SnapTray::Audio::TimestampedPcmMixer>(mixerConfig);
    m_reportedMixerDrop = false;
    m_reportedMicrophoneFormatFailure = false;
    m_reportedSystemFormatFailure = false;
    m_microphoneActive = false;
    m_systemAudioActive = false;
    m_lastNotifiedActiveSource = static_cast<int>(m_source);
    m_stopping = false;
    m_systemAudioFailed = false;

    // Check microphone permission if needed
    bool microphoneAvailable = false;
    if (wantsMicrophone) {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        if (status == AVAuthorizationStatusNotDetermined) {
            __block bool granted = false;
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL g) {
                granted = g;
                dispatch_semaphore_signal(semaphore);
            }];

            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

            microphoneAvailable = granted;
            if (!granted && m_source == AudioSource::Microphone) {
                emit error("Microphone access denied");
                m_mixer.reset();
                return false;
            } else if (!granted) {
                qWarning() << "CoreAudioCaptureEngine: Microphone access denied;"
                              " continuing with system audio";
            }
        } else if (status == AVAuthorizationStatusAuthorized) {
            microphoneAvailable = true;
        } else {
            // Denied or Restricted
            if (m_source == AudioSource::Microphone) {
                emit error("Microphone access not authorized. Please enable in System Settings.");
                m_mixer.reset();
                return false;
            }
            qWarning() << "CoreAudioCaptureEngine: Microphone access denied;"
                          " continuing with system audio";
        }
    }

    // Initialize timing
    m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration = 0;

    // Create capture queue
    d->captureQueue = dispatch_queue_create("com.snaptray.audio.capture", DISPATCH_QUEUE_SERIAL);
    dispatch_queue_set_specific(d->captureQueue,
                                &kCaptureQueueSpecificKey,
                                d,
                                nullptr);
    // Capture callbacks may begin as soon as startRunning/startCapture returns.
    // Open the processing gate before either native source is started.
    m_running = true;
    m_paused = false;

    auto setupMicrophone = [&]() -> bool {
        if (!microphoneAvailable || !wantsMicrophone) {
            return false;
        }

        d->captureSession = [[AVCaptureSession alloc] init];

        // Get the audio device
        AVCaptureDevice *audioDevice = nil;
        if (m_deviceId.isEmpty()) {
            audioDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
        } else {
            audioDevice = [AVCaptureDevice deviceWithUniqueID:m_deviceId.toNSString()];
        }

        if (!audioDevice) {
            qWarning() << "CoreAudioCaptureEngine: No audio input device found";
            return false;
        }

        NSError *error = nil;
        d->audioInput = [AVCaptureDeviceInput deviceInputWithDevice:audioDevice error:&error];
        if (error || !d->audioInput) {
            qWarning() << "CoreAudioCaptureEngine: Failed to create microphone input:"
                       << (error ? QString::fromNSString(error.localizedDescription)
                                 : QStringLiteral("unknown error"));
            return false;
        }

        if ([d->captureSession canAddInput:d->audioInput]) {
            [d->captureSession addInput:d->audioInput];
        } else {
            qWarning() << "CoreAudioCaptureEngine: Cannot add microphone input";
            return false;
        }

        // Create audio output
        d->audioOutput = [[AVCaptureAudioDataOutput alloc] init];
        d->audioOutput.audioSettings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @16,
            AVLinearPCMIsFloatKey: @NO,
            AVLinearPCMIsBigEndianKey: @NO,
            AVLinearPCMIsNonInterleaved: @NO,
        };
        d->audioDelegate = [[AudioCaptureDelegate alloc] init];
        d->audioDelegate.engine = this;
        d->audioDelegate.invalidated = NO;
        d->audioDelegate.startTime = m_startTime;
        d->audioDelegate.pausedDuration = 0;
        d->audioDelegate.paused = false;
        d->audioDelegate.hasTimelineAnchor = NO;

        [d->audioOutput setSampleBufferDelegate:d->audioDelegate queue:d->captureQueue];

        if ([d->captureSession canAddOutput:d->audioOutput]) {
            [d->captureSession addOutput:d->audioOutput];
        } else {
            qWarning() << "CoreAudioCaptureEngine: Cannot add microphone output";
            return false;
        }

        return true;
    };

    bool microphoneActive = setupMicrophone();
    m_microphoneActive = microphoneActive;
    if (wantsMicrophone && !microphoneActive) {
        d->cleanupMicrophone();
        m_mixer->setSourceEnabled(SnapTray::Audio::Source::Microphone, false, 0);

        if (!wantsSystemAudio) {
            emit error("Microphone capture could not start");
            m_running = false;
            d->cleanup();
            m_mixer.reset();
            return false;
        }
        qWarning() << "CoreAudioCaptureEngine: Microphone unavailable;"
                      " continuing with system audio";
    }

    bool systemAudioActive = false;

#if HAS_SCREENCAPTUREKIT
    // Set up system audio capture (macOS 13+)
    if ((m_source == AudioSource::SystemAudio || m_source == AudioSource::Both) &&
        isSystemAudioSupported()) {
        if (@available(macOS 13.0, *)) {
            qDebug() << "CoreAudioCaptureEngine: Setting up ScreenCaptureKit audio...";

            __block SCShareableContent *contentResult = nil;
            __block NSError *contentError = nil;

            @try {
                // Get shareable content (requires screen recording permission)
                dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

                [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *shareableContent, NSError *error) {
                    contentResult = shareableContent;
                    contentError = error;
                    dispatch_semaphore_signal(semaphore);
                }];

                dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
                qDebug() << "CoreAudioCaptureEngine: Got shareable content, error:" << (contentError != nil);

                if (contentError) {
                    qWarning() << "CoreAudioCaptureEngine: Content error:" << QString::fromNSString(contentError.localizedDescription);
                } else if (!contentResult) {
                    qWarning() << "CoreAudioCaptureEngine: No shareable content available";
                } else {
                    // Immediately capture displays array to prevent ARC issues
                    qDebug() << "CoreAudioCaptureEngine: Checking displays...";
                    NSArray<SCDisplay *> *displays = contentResult.displays;
                    qDebug() << "CoreAudioCaptureEngine: Displays array:" << (displays ? "valid" : "nil");

                    if (!displays || displays.count == 0) {
                        qWarning() << "CoreAudioCaptureEngine: No displays available";
                    } else {
                        // Create a content filter for audio-only capture
                        SCDisplay *display = displays.firstObject;
                        qDebug() << "CoreAudioCaptureEngine: Got display:" << (display ? "valid" : "nil");

                        if (!display) {
                            qWarning() << "CoreAudioCaptureEngine: First display is nil";
                        } else {
                            qDebug() << "CoreAudioCaptureEngine: Creating content filter for display...";

                            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
                            if (!filter) {
                                qWarning() << "CoreAudioCaptureEngine: Failed to create content filter";
                            } else {
                                // Configure for audio capture
                                SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
                                config.capturesAudio = YES;
                                config.excludesCurrentProcessAudio = YES;
                                config.width = 2;  // Minimal video (must be at least 2)
                                config.height = 2;
                                config.minimumFrameInterval = CMTimeMake(1, 1);  // 1 fps minimum
                                config.sampleRate = 48000;
                                config.channelCount = 2;

                                qDebug() << "CoreAudioCaptureEngine: Creating delegate...";
                                d->systemAudioFailureBridge =
                                    std::make_shared<SystemAudioFailureBridge>();
                                {
                                    QMutexLocker locker(
                                        &d->systemAudioFailureBridge->mutex);
                                    d->systemAudioFailureBridge->engine = this;
                                }
                                d->scDelegate = [[SCKAudioCaptureDelegate alloc] init];
                                d->scDelegate->failureBridge =
                                    d->systemAudioFailureBridge;
                                d->scDelegate.engine = this;
                                d->scDelegate.invalidated = NO;
                                d->scDelegate.startTime = m_startTime;
                                d->scDelegate.pausedDuration = 0;
                                d->scDelegate.paused = false;
                                d->scDelegate.hasTimelineAnchor = NO;

                                qDebug() << "CoreAudioCaptureEngine: Creating SCStream...";
                                d->scStream = [[SCStream alloc]
                                    initWithFilter:filter
                                    configuration:config
                                    delegate:d->scDelegate];

                                if (!d->scStream) {
                                    qWarning() << "CoreAudioCaptureEngine: Failed to create SCStream";
                                    d->cleanupSystemAudio();
                                } else {
                                    qDebug() << "CoreAudioCaptureEngine: Adding stream output...";
                                    NSError *addError = nil;
                                    BOOL added = [d->scStream addStreamOutput:d->scDelegate type:SCStreamOutputTypeAudio sampleHandlerQueue:d->captureQueue error:&addError];
                                    d->scOutputAdded = added;

                                    if (!added || addError) {
                                        qWarning() << "CoreAudioCaptureEngine: Failed to add stream output:"
                                                   << (addError ? QString::fromNSString(addError.localizedDescription) : "unknown error");
                                        d->cleanupSystemAudio();
                                    } else {
                                        qDebug() << "CoreAudioCaptureEngine: Starting capture...";
                                        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
                                        __block NSError *startError = nil;
                                        m_systemAudioActive = true;

                                        [d->scStream startCaptureWithCompletionHandler:^(NSError *error) {
                                            startError = error;
                                            dispatch_semaphore_signal(startSem);
                                        }];

                                        dispatch_semaphore_wait(startSem, DISPATCH_TIME_FOREVER);
                                        qDebug() << "CoreAudioCaptureEngine: Capture start completed, error:" << (startError != nil);

                                        if (startError) {
                                            m_systemAudioActive = false;
                                            qWarning() << "CoreAudioCaptureEngine: Start error:" << QString::fromNSString(startError.localizedDescription);
                                            d->cleanupSystemAudio();
                                        } else {
                                            d->scCaptureStarted = true;
                                            systemAudioActive =
                                                !m_reportedSystemFormatFailure.load();
                                            m_systemAudioActive = systemAudioActive;
                                            qDebug() << "CoreAudioCaptureEngine: System audio capture active";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            @catch (NSException *exception) {
                m_systemAudioActive = false;
                qWarning() << "CoreAudioCaptureEngine: Exception during SCK setup:"
                           << QString::fromNSString(exception.name) << "-" << QString::fromNSString(exception.reason);
                d->cleanupSystemAudio();
            }
            @finally {
                contentResult = nil;
                contentError = nil;
            }
        }
    }
#endif

    if (microphoneActive) {
        [d->captureSession startRunning];
        microphoneActive = d->captureSession.running
            && !m_reportedMicrophoneFormatFailure.load();
        m_microphoneActive = microphoneActive;
        if (!microphoneActive) {
            d->cleanupMicrophone();
            m_mixer->setSourceEnabled(SnapTray::Audio::Source::Microphone, false, 0);
            if (!systemAudioActive) {
                emit error("Microphone capture could not start");
                m_running = false;
                d->cleanup();
                m_mixer.reset();
                return false;
            }
        }
    }

    if (wantsSystemAudio && !systemAudioActive) {
        m_systemAudioActive = false;
#if HAS_SCREENCAPTUREKIT
        d->cleanupSystemAudio();
#endif
        m_mixer->setSourceEnabled(SnapTray::Audio::Source::SystemAudio, false, 0);
        if (!microphoneActive) {
            emit error("System audio capture could not start");
            m_running = false;
            d->cleanup();
            m_mixer.reset();
            return false;
        }
    }

    if (!microphoneActive && !systemAudioActive) {
        emit error("No requested audio source could be started");
        m_running = false;
        d->cleanup();
        m_mixer.reset();
        return false;
    }

    m_microphoneActive = microphoneActive;
    m_systemAudioActive = systemAudioActive;
    m_starting = false;
    notifyActiveSourceChanged();

    qDebug() << "CoreAudioCaptureEngine: Started with source" << static_cast<int>(m_source);
    return true;
}

void CoreAudioCaptureEngine::processCapturedAudio(
    SnapTray::Audio::Source source,
    const QByteArray& pcm,
    const SnapTray::Audio::Pcm16Format& format,
    qint64 timestampNs)
{
    if (!m_running || m_paused || !m_mixer) {
        return;
    }

    const SnapTray::Audio::InputChunk chunk{pcm, timestampNs, format};
    const auto result = m_mixer->push(source, chunk);
    deliverMixerOutput(result);

    if (!m_reportedMixerDrop
        && (result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::AcceptedWithDrops
            || result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::TooLate)) {
        m_reportedMixerDrop = true;
        emit warning("Audio capture fell behind; a short section may be silent.");
    } else if (!m_reportedMixerDrop
               && (result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::MalformedInput
                   || result.code == SnapTray::Audio::TimestampedPcmMixer::ResultCode::UnsupportedFormat)) {
        m_reportedMixerDrop = true;
        emit warning("An audio source produced an unsupported format and was skipped.");
    }
}

void CoreAudioCaptureEngine::reportCapturedAudioFormatFailure(
    SnapTray::Audio::Source source,
    qint64 effectiveTimeNs)
{
    if (!m_running || !m_mixer) {
        return;
    }

    std::atomic<bool>& alreadyReported = source == SnapTray::Audio::Source::Microphone
        ? m_reportedMicrophoneFormatFailure
        : m_reportedSystemFormatFailure;
    if (alreadyReported.exchange(true)) {
        return;
    }

    if (source == SnapTray::Audio::Source::Microphone) {
        m_microphoneActive = false;
        qWarning() << "CoreAudioCaptureEngine: Disabling microphone after"
                      " unsupported audio format";
    } else {
        m_systemAudioActive = false;
        qWarning() << "CoreAudioCaptureEngine: Disabling system audio after"
                      " unsupported audio format";
    }
    deliverMixerOutput(m_mixer->setSourceEnabled(
        source,
        false,
        qMax<qint64>(qint64(0), effectiveTimeNs)));
    notifyActiveSourceChanged();

    const QPointer<CoreAudioCaptureEngine> engineGuard(this);
    QMetaObject::invokeMethod(this, [engineGuard, source]() {
        if (!engineGuard || !engineGuard->m_running) {
            return;
        }
        if (source == SnapTray::Audio::Source::Microphone) {
            engineGuard->d->cleanupMicrophone();
        } else {
#if HAS_SCREENCAPTUREKIT
            engineGuard->d->cleanupSystemAudio();
#endif
        }
    }, Qt::QueuedConnection);
}

void CoreAudioCaptureEngine::handleSystemAudioCaptureFailure(
    const QString& details)
{
    if (!m_running || !m_mixer || m_systemAudioFailed) {
        return;
    }
    m_systemAudioFailed = true;
    m_systemAudioActive = false;
    qWarning() << "CoreAudioCaptureEngine: Handling system audio failure:"
               << details;

    qint64 activeTimeNs = 0;
    {
        QMutexLocker locker(&m_timingMutex);
        const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const qint64 activeNow = m_paused ? m_pauseStartTime : now;
        activeTimeNs = qMax<qint64>(
            qint64(0),
            (activeNow - m_startTime - m_pausedDuration) * NSEC_PER_MSEC);
    }

#if HAS_SCREENCAPTUREKIT
    d->cleanupSystemAudio();
#endif
    deliverMixerOutput(m_mixer->setSourceEnabled(
        SnapTray::Audio::Source::SystemAudio,
        false,
        activeTimeNs));

    notifyActiveSourceChanged();
}

void CoreAudioCaptureEngine::deliverMixerOutput(
    const SnapTray::Audio::TimestampedPcmMixer::ProcessResult& result)
{
    for (const auto& output : result.output) {
        if (!output.pcm.isEmpty()) {
            emit audioDataReady(output.pcm, output.startFrame);
        }
    }
}

void CoreAudioCaptureEngine::stop()
{
    if (!m_running) return;
    m_stopping = true;

    qint64 endTimeNs = 0;
    {
        QMutexLocker locker(&m_timingMutex);
        const qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const qint64 activeNow = m_paused ? m_pauseStartTime : now;
        endTimeNs = qMax<qint64>(
            qint64(0),
            (activeNow - m_startTime - m_pausedDuration) * NSEC_PER_MSEC);
    }

    // Invalidate both native sources and drain their serial callback queue
    // while the processing gate is still open. This lets every sample already
    // in flight reach the mixer before its final flush.
    d->cleanup();
    m_microphoneActive = false;
    m_systemAudioActive = false;
    m_running = false;

    if (m_mixer) {
        deliverMixerOutput(m_mixer->flush(endTimeNs));
    }

    m_mixer.reset();

    m_paused = false;
    m_stopping = false;

    qDebug() << "CoreAudioCaptureEngine: Stopped";
}

void CoreAudioCaptureEngine::disposeAsync()
{
    stop();
    QObject::disconnect(this, nullptr, nullptr, nullptr);
    deleteLater();
}

void CoreAudioCaptureEngine::pause()
{
    if (!m_running || m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    m_pauseStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // Publish pause on the same serial queue used by both delegates. The sync
    // is also a delivery barrier for every pre-pause sample buffer.
    d->updateDelegatePauseState(true, m_pausedDuration, false);
    m_paused = true;
    if (m_mixer) {
        const qint64 activeTimeNs = qMax<qint64>(
            qint64(0),
            (m_pauseStartTime - m_startTime - m_pausedDuration) * NSEC_PER_MSEC);
        deliverMixerOutput(m_mixer->pause(activeTimeNs));
    }

    qDebug() << "CoreAudioCaptureEngine: Paused";
}

void CoreAudioCaptureEngine::resume()
{
    if (!m_running || !m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration += (now - m_pauseStartTime);
    m_paused = false;
    if (m_mixer) {
        const qint64 activeTimeNs = qMax<qint64>(
            qint64(0),
            (now - m_startTime - m_pausedDuration) * NSEC_PER_MSEC);
        m_mixer->resume(activeTimeNs);
    }

    // Reset timing state while delegates are still paused; paused=false is
    // written last inside the capture queue block.
    d->updateDelegatePauseState(false, m_pausedDuration, true);

    qDebug() << "CoreAudioCaptureEngine: Resumed";
}

// ========== Permission Helper Functions ==========
// These are called from IAudioCaptureEngine.cpp via extern declarations

IAudioCaptureEngine::MicrophonePermission checkMicrophonePermissionMac()
{
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    switch (status) {
        case AVAuthorizationStatusAuthorized:
            return IAudioCaptureEngine::MicrophonePermission::Authorized;
        case AVAuthorizationStatusDenied:
            return IAudioCaptureEngine::MicrophonePermission::Denied;
        case AVAuthorizationStatusRestricted:
            return IAudioCaptureEngine::MicrophonePermission::Restricted;
        case AVAuthorizationStatusNotDetermined:
        default:
            return IAudioCaptureEngine::MicrophonePermission::NotDetermined;
    }
}

void requestMicrophonePermissionMac(std::function<void(bool)> callback)
{
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (callback) {
                callback(granted == YES);
            }
        });
    }];
}

#endif // Q_OS_MAC
