#include "capture/CoreAudioCaptureEngine.h"

#ifdef Q_OS_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>

#include <QDebug>
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

@implementation AudioCaptureDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection
{
    if (self.paused || !self.engine) return;

    // Get audio buffer
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) return;

    size_t length = 0;
    char *dataPointer = nullptr;
    CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &length, &dataPointer);

    if (dataPointer && length > 0) {
        QByteArray audioData(dataPointer, static_cast<int>(length));

        // Calculate timestamp
        qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        qint64 timestamp = now - self.startTime - self.pausedDuration;

        // Emit through Qt signal (must be done carefully from Obj-C context)
        QMetaObject::invokeMethod(self.engine, [=]() {
            emit self.engine->audioDataReady(audioData, timestamp);
        }, Qt::QueuedConnection);
    }
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

API_AVAILABLE(macos(13.0))
@implementation SCKAudioCaptureDelegate

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio) return;
    if (self.paused || !self.engine) return;

    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) return;

    size_t length = 0;
    char *dataPointer = nullptr;
    CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &length, &dataPointer);

    if (dataPointer && length > 0) {
        QByteArray audioData(dataPointer, static_cast<int>(length));

        qint64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        qint64 timestamp = now - self.startTime - self.pausedDuration;

        QMetaObject::invokeMethod(self.engine, [=]() {
            emit self.engine->audioDataReady(audioData, timestamp);
        }, Qt::QueuedConnection);
    }
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
    // Check if we have permission to access the microphone
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

    // Request microphone permission if needed
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

            if (!granted) {
                emit error("Microphone access denied");
                return false;
            }
        } else if (status != AVAuthorizationStatusAuthorized) {
            emit error("Microphone access not authorized. Please enable in System Settings.");
            return false;
        }
    }

    // Initialize timing
    m_startTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    m_pausedDuration = 0;

    // Create capture queue
    d->captureQueue = dispatch_queue_create("com.snaptray.audio.capture", DISPATCH_QUEUE_SERIAL);

    // Set up microphone capture
    if (m_source == AudioSource::Microphone || m_source == AudioSource::Both) {
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
            // Get shareable content
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            __block SCShareableContent *content = nil;
            __block NSError *contentError = nil;

            [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *shareableContent, NSError *error) {
                content = shareableContent;
                contentError = error;
                dispatch_semaphore_signal(semaphore);
            }];

            dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

            if (contentError || !content) {
                emit warning("Cannot access system audio");
            } else {
                // Create a content filter for audio-only capture
                SCDisplay *display = content.displays.firstObject;
                if (display) {
                    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];

                    // Configure for audio capture
                    SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
                    config.capturesAudio = YES;
                    config.excludesCurrentProcessAudio = YES;
                    config.width = 1;  // Minimal video
                    config.height = 1;

                    // Create stream
                    d->scStream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
                    d->scDelegate = [[SCKAudioCaptureDelegate alloc] init];
                    d->scDelegate.engine = this;
                    d->scDelegate.startTime = m_startTime;
                    d->scDelegate.pausedDuration = 0;
                    d->scDelegate.paused = false;

                    NSError *addError = nil;
                    [d->scStream addStreamOutput:d->scDelegate type:SCStreamOutputTypeAudio sampleHandlerQueue:d->captureQueue error:&addError];

                    if (addError) {
                        emit warning(QString("Cannot add audio output: %1").arg(
                            QString::fromNSString(addError.localizedDescription)));
                    } else {
                        dispatch_semaphore_t startSem = dispatch_semaphore_create(0);
                        __block NSError *startError = nil;

                        [d->scStream startCaptureWithCompletionHandler:^(NSError *error) {
                            startError = error;
                            dispatch_semaphore_signal(startSem);
                        }];

                        dispatch_semaphore_wait(startSem, DISPATCH_TIME_FOREVER);

                        if (startError) {
                            emit warning(QString("Cannot start system audio: %1").arg(
                                QString::fromNSString(startError.localizedDescription)));
                            d->scStream = nil;
                        }
                    }
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

#endif // Q_OS_MAC
