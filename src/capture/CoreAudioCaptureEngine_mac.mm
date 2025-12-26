#include "capture/CoreAudioCaptureEngine.h"

#ifdef Q_OS_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>

#include <QDebug>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <chrono>

// Check if ScreenCaptureKit is available (macOS 12.3+)
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120300
#define HAS_SCREENCAPTUREKIT 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

// Objective-C delegate for audio capture
@interface AudioCaptureDelegate : NSObject <AVCaptureAudioDataOutputSampleBufferDelegate>
@property (nonatomic, assign) CoreAudioCaptureEngine *engine;
@property (nonatomic, assign) qint64 startTime;
@property (nonatomic, assign) qint64 pausedDuration;
@property (nonatomic, assign) bool paused;
@end

static QByteArray convertAudioBufferToPCM16(CMSampleBufferRef sampleBuffer,
                                            const char *label,
                                            bool *formatLogged)
{
    CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (!asbd) return QByteArray();

    CMItemCount numFrames = CMSampleBufferGetNumSamples(sampleBuffer);
    if (numFrames <= 0) return QByteArray();

    int channels = static_cast<int>(asbd->mChannelsPerFrame);
    if (channels <= 0) return QByteArray();

    bool isFloat = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
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
    } else if (!isFloat && bitsPerChannel == 16) {
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
        output.clear();
    }

    if (blockBuffer) {
        CFRelease(blockBuffer);
    }
    free(bufferList);
    return output;
}

static QByteArray convertMicAudioBufferToPCM16(CMSampleBufferRef sampleBuffer)
{
    static bool micFormatLogged = false;
    return convertAudioBufferToPCM16(sampleBuffer, "Mic", &micFormatLogged);
}

@implementation AudioCaptureDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection
{
    if (self.paused || !self.engine) return;

    // Convert to 16-bit PCM
    QByteArray audioData = convertMicAudioBufferToPCM16(sampleBuffer);
    if (audioData.isEmpty()) return;

    qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    qint64 timestamp = now - self.startTime - self.pausedDuration;

    QMetaObject::invokeMethod(self.engine, [=]() {
        emit self.engine->audioDataReady(audioData, timestamp);
    }, Qt::QueuedConnection);
}

@end

#if HAS_SCREENCAPTUREKIT
// ScreenCaptureKit delegate for system audio (macOS 13+)
API_AVAILABLE(macos(13.0))
@interface SCKAudioCaptureDelegate : NSObject <SCStreamDelegate, SCStreamOutput>
@property (nonatomic, assign) CoreAudioCaptureEngine *engine;
@property (nonatomic, assign) qint64 startTime;
@property (nonatomic, assign) qint64 pausedDuration;
@property (nonatomic, assign) bool paused;
@end

static QByteArray convertSckAudioBufferToPCM16(CMSampleBufferRef sampleBuffer)
{
    static bool sckFormatLogged = false;
    return convertAudioBufferToPCM16(sampleBuffer, "SCK", &sckFormatLogged);
}

API_AVAILABLE(macos(13.0))
@implementation SCKAudioCaptureDelegate

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio) return;
    if (self.paused || !self.engine) return;

    // Convert to 16-bit PCM
    QByteArray audioData = convertSckAudioBufferToPCM16(sampleBuffer);
    if (audioData.isEmpty()) return;

    qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    qint64 timestamp = now - self.startTime - self.pausedDuration;

    QMetaObject::invokeMethod(self.engine, [=]() {
        emit self.engine->audioDataReady(audioData, timestamp);
    }, Qt::QueuedConnection);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    if (error) {
        QString errorMsg = QString::fromNSString(error.localizedDescription);
        QMetaObject::invokeMethod(self.engine, [=]() {
            emit self.engine->error(errorMsg);
        }, Qt::QueuedConnection);
    }
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
#endif

    ~Private() {
        cleanup();
    }

    void cleanup() {
        if (captureSession) {
            [captureSession stopRunning];
            captureSession = nil;
        }
        audioInput = nil;
        audioOutput = nil;
        audioDelegate = nil;

#if HAS_SCREENCAPTUREKIT
        if (@available(macOS 13.0, *)) {
            if (scStream) {
                [scStream stopCaptureWithCompletionHandler:^(NSError *error) {}];
                scStream = nil;
            }
            scDelegate = nil;
        }
#endif

        if (captureQueue) {
            captureQueue = nil;
        }
    }
};

CoreAudioCaptureEngine::CoreAudioCaptureEngine(QObject *parent)
    : IAudioCaptureEngine(parent)
    , d(new Private)
{
    // Set default format
    m_format.sampleRate = 48000;
    m_format.channels = 2;
    m_format.bitsPerSample = 16;
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
        qWarning() << "CoreAudioCaptureEngine: Cannot change source while running";
        return false;
    }

    // Check if system audio is requested but not supported
    if ((source == AudioSource::SystemAudio || source == AudioSource::Both) &&
        !isSystemAudioSupported()) {
        qWarning() << "CoreAudioCaptureEngine: System audio not supported on this macOS version";
        emit warning("System audio capture requires macOS 13 or later");
    }

    m_source = source;
    return true;
}

bool CoreAudioCaptureEngine::setDevice(const QString &deviceId)
{
    if (m_running) {
        qWarning() << "CoreAudioCaptureEngine: Cannot change device while running";
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

bool CoreAudioCaptureEngine::start()
{
    if (m_running) {
        qWarning() << "CoreAudioCaptureEngine: Already running";
        return false;
    }

    if (m_source == AudioSource::None) {
        qWarning() << "CoreAudioCaptureEngine: No audio source configured";
        return false;
    }

    // Check microphone permission if needed
    bool microphoneAvailable = false;
    if (m_source == AudioSource::Microphone || m_source == AudioSource::Both) {
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
                return false;
            } else if (!granted) {
                emit warning("Microphone access denied. Recording system audio only.");
            }
        } else if (status == AVAuthorizationStatusAuthorized) {
            microphoneAvailable = true;
        } else {
            // Denied or Restricted
            if (m_source == AudioSource::Microphone) {
                emit error("Microphone access not authorized. Please enable in System Settings.");
                return false;
            }
            // For "Both" mode, warn but continue with system audio only
            emit warning("Microphone access denied. Recording system audio only.");
        }
    }

    // Initialize timing
    m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration = 0;

    // Create capture queue
    d->captureQueue = dispatch_queue_create("com.snaptray.audio.capture", DISPATCH_QUEUE_SERIAL);

    // Set up microphone capture (only if permission granted)
    if (microphoneAvailable && (m_source == AudioSource::Microphone || m_source == AudioSource::Both)) {
        d->captureSession = [[AVCaptureSession alloc] init];

        // Get the audio device
        AVCaptureDevice *audioDevice = nil;
        if (m_deviceId.isEmpty()) {
            audioDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
        } else {
            audioDevice = [AVCaptureDevice deviceWithUniqueID:m_deviceId.toNSString()];
        }

        if (!audioDevice) {
            emit error("No audio input device found");
            d->cleanup();
            return false;
        }

        NSError *error = nil;
        d->audioInput = [AVCaptureDeviceInput deviceInputWithDevice:audioDevice error:&error];
        if (error || !d->audioInput) {
            emit this->error(QString::fromNSString(error.localizedDescription));
            d->cleanup();
            return false;
        }

        if ([d->captureSession canAddInput:d->audioInput]) {
            [d->captureSession addInput:d->audioInput];
        } else {
            emit this->error("Cannot add audio input to capture session");
            d->cleanup();
            return false;
        }

        // Create audio output
        d->audioOutput = [[AVCaptureAudioDataOutput alloc] init];
        d->audioDelegate = [[AudioCaptureDelegate alloc] init];
        d->audioDelegate.engine = this;
        d->audioDelegate.startTime = m_startTime;
        d->audioDelegate.pausedDuration = 0;
        d->audioDelegate.paused = false;

        [d->audioOutput setSampleBufferDelegate:d->audioDelegate queue:d->captureQueue];

        if ([d->captureSession canAddOutput:d->audioOutput]) {
            [d->captureSession addOutput:d->audioOutput];
        } else {
            emit this->error("Cannot add audio output to capture session");
            d->cleanup();
            return false;
        }

        [d->captureSession startRunning];
    }

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
                    contentResult = [shareableContent retain];
                    contentError = [error retain];
                    dispatch_semaphore_signal(semaphore);
                }];

                dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
                qDebug() << "CoreAudioCaptureEngine: Got shareable content, error:" << (contentError != nil);

                if (contentError) {
                    qWarning() << "CoreAudioCaptureEngine: Content error:" << QString::fromNSString(contentError.localizedDescription);
                    emit warning("Cannot access system audio. Please grant Screen Recording permission in System Settings.");
                } else if (!contentResult) {
                    qWarning() << "CoreAudioCaptureEngine: No shareable content available";
                    emit warning("Cannot access system audio");
                } else {
                    // Immediately capture displays array to prevent ARC issues
                    qDebug() << "CoreAudioCaptureEngine: Checking displays...";
                    NSArray<SCDisplay *> *displays = contentResult.displays;
                    qDebug() << "CoreAudioCaptureEngine: Displays array:" << (displays ? "valid" : "nil");

                    if (!displays || displays.count == 0) {
                        qWarning() << "CoreAudioCaptureEngine: No displays available";
                        emit warning("Cannot access system audio - no displays found");
                    } else {
                        // Create a content filter for audio-only capture
                        SCDisplay *display = displays.firstObject;
                        qDebug() << "CoreAudioCaptureEngine: Got display:" << (display ? "valid" : "nil");

                        if (!display) {
                            qWarning() << "CoreAudioCaptureEngine: First display is nil";
                            emit warning("Cannot access system audio - display not found");
                        } else {
                            qDebug() << "CoreAudioCaptureEngine: Creating content filter for display...";

                            SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
                            if (!filter) {
                                qWarning() << "CoreAudioCaptureEngine: Failed to create content filter";
                                emit warning("Cannot create audio capture filter");
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

                                qDebug() << "CoreAudioCaptureEngine: Creating SCStream...";
                                d->scStream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];

                                if (!d->scStream) {
                                    qWarning() << "CoreAudioCaptureEngine: Failed to create SCStream";
                                    emit warning("Cannot create audio stream");
                                } else {
                                    qDebug() << "CoreAudioCaptureEngine: Creating delegate...";
                                    d->scDelegate = [[SCKAudioCaptureDelegate alloc] init];
                                    d->scDelegate.engine = this;
                                    d->scDelegate.startTime = m_startTime;
                                    d->scDelegate.pausedDuration = 0;
                                    d->scDelegate.paused = false;

                                    qDebug() << "CoreAudioCaptureEngine: Adding stream output...";
                                    NSError *addError = nil;
                                    BOOL added = [d->scStream addStreamOutput:d->scDelegate type:SCStreamOutputTypeAudio sampleHandlerQueue:d->captureQueue error:&addError];

                                    if (!added || addError) {
                                        qWarning() << "CoreAudioCaptureEngine: Failed to add stream output:"
                                                   << (addError ? QString::fromNSString(addError.localizedDescription) : "unknown error");
                                        emit warning(QString("Cannot add audio output: %1").arg(
                                            addError ? QString::fromNSString(addError.localizedDescription) : "unknown error"));
                                        d->scStream = nil;
                                        d->scDelegate = nil;
                                    } else {
                                        qDebug() << "CoreAudioCaptureEngine: Starting capture...";
                                        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
                                        __block NSError *startError = nil;

                                        [d->scStream startCaptureWithCompletionHandler:^(NSError *error) {
                                            startError = [error retain];
                                            dispatch_semaphore_signal(startSem);
                                        }];

                                        dispatch_semaphore_wait(startSem, DISPATCH_TIME_FOREVER);
                                        qDebug() << "CoreAudioCaptureEngine: Capture start completed, error:" << (startError != nil);

                                        if (startError) {
                                            qWarning() << "CoreAudioCaptureEngine: Start error:" << QString::fromNSString(startError.localizedDescription);
                                            emit warning(QString("Cannot start system audio: %1").arg(
                                                QString::fromNSString(startError.localizedDescription)));
                                            d->scStream = nil;
                                            d->scDelegate = nil;
                                        } else {
                                            qDebug() << "CoreAudioCaptureEngine: System audio capture active";
                                        }

                                        if (startError) {
                                            [startError release];
                                            startError = nil;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            @catch (NSException *exception) {
                qWarning() << "CoreAudioCaptureEngine: Exception during SCK setup:"
                           << QString::fromNSString(exception.name) << "-" << QString::fromNSString(exception.reason);
                emit warning("System audio capture failed");
                d->scStream = nil;
                d->scDelegate = nil;
            }
            @finally {
                if (contentResult) {
                    [contentResult release];
                    contentResult = nil;
                }
                if (contentError) {
                    [contentError release];
                    contentError = nil;
                }
            }
        }
    }
#endif

    m_running = true;
    m_paused = false;

    qDebug() << "CoreAudioCaptureEngine: Started with source" << static_cast<int>(m_source);
    return true;
}

void CoreAudioCaptureEngine::stop()
{
    if (!m_running) return;

    d->cleanup();

    m_running = false;
    m_paused = false;

    qDebug() << "CoreAudioCaptureEngine: Stopped";
}

void CoreAudioCaptureEngine::pause()
{
    if (!m_running || m_paused) return;

    QMutexLocker locker(&m_timingMutex);
    m_pauseStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_paused = true;

    if (d->audioDelegate) {
        d->audioDelegate.paused = true;
    }
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (d->scDelegate) {
            d->scDelegate.paused = true;
        }
    }
#endif

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

    if (d->audioDelegate) {
        d->audioDelegate.paused = false;
        d->audioDelegate.pausedDuration = m_pausedDuration;
    }
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 13.0, *)) {
        if (d->scDelegate) {
            d->scDelegate.paused = false;
            d->scDelegate.pausedDuration = m_pausedDuration;
        }
    }
#endif

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
