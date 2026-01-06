#include "capture/SCKCaptureEngine.h"

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <mutex>
#include <atomic>

// ScreenCaptureKit is only available on macOS 12.3+
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 120300
#define HAS_SCREENCAPTUREKIT 1
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#else
#define HAS_SCREENCAPTUREKIT 0
#endif

// Forward declare the delegate class
#if HAS_SCREENCAPTUREKIT
API_AVAILABLE(macos(12.3))
@interface SCKStreamDelegate : NSObject <SCStreamDelegate, SCStreamOutput>
@property (atomic, assign) SCKCaptureEngine::Private *engine;  // atomic for thread safety
@property (atomic, assign) BOOL invalidated;
@property (nonatomic, strong) dispatch_group_t processingGroup;  // Track active callbacks
- (instancetype)init;
@end
#endif

class SCKCaptureEngine::Private
{
public:
    Private() = default;
    ~Private() { cleanup(); }

    void cleanup();

#if HAS_SCREENCAPTUREKIT
    API_AVAILABLE(macos(12.3))
    SCStream *stream = nil;

    API_AVAILABLE(macos(12.3))
    SCKStreamDelegate *delegate = nil;

    API_AVAILABLE(macos(12.3))
    SCDisplay *targetDisplay = nil;
#endif

    QRect captureRegion;
    QScreen *targetScreen = nullptr;
    int frameRate = 30;
    std::atomic<bool> running{false};
    bool useScreenCaptureKit = false;

    // Windows to exclude from capture (stored as CGWindowID)
    QList<CGWindowID> excludedWindowIds;

    // Latest captured frame (for async delivery)
    QImage latestFrame;
    std::mutex frameMutex;

    void setLatestFrame(const QImage &frame) {
        std::lock_guard<std::mutex> lock(frameMutex);
        latestFrame = frame;
    }

    QImage getLatestFrame() {
        std::lock_guard<std::mutex> lock(frameMutex);
        return latestFrame;
    }
};

#if HAS_SCREENCAPTUREKIT
API_AVAILABLE(macos(12.3))
@implementation SCKStreamDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _processingGroup = dispatch_group_create();
        _invalidated = NO;
        _engine = nil;
    }
    return self;
}

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
        ofType:(SCStreamOutputType)type
{
    // Enter group to mark callback is active (for cleanup synchronization)
    dispatch_group_enter(_processingGroup);

    // Use @try/@finally to ensure dispatch_group_leave is always called
    @try {
        static int frameCount = 0;
        frameCount++;

        // Atomically read engine pointer to local variable
        SCKCaptureEngine::Private *localEngine = self.engine;

        // Log every 30 frames (once per second at 30fps)
        if (frameCount % 30 == 1) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - frame:" << frameCount
                     << "invalidated:" << (_invalidated ? "YES" : "NO")
                     << "engine:" << (localEngine ? "valid" : "nil");
        }

        // Check invalidated FIRST - use local engine copy for consistency
        if (_invalidated || type != SCStreamOutputTypeScreen || !localEngine) {
            if (frameCount <= 5) {
                qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - SKIPPED (invalidated or wrong type)";
            }
            return;  // @finally will execute dispatch_group_leave
        }

        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!imageBuffer) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - imageBuffer is NULL";
            return;
        }

        CVReturn lockResult = CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        if (lockResult != kCVReturnSuccess) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - Failed to lock pixel buffer:" << lockResult;
            return;
        }

        void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
        size_t width = CVPixelBufferGetWidth(imageBuffer);
        size_t height = CVPixelBufferGetHeight(imageBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);

        if (frameCount <= 5) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - buffer info:"
                     << "baseAddress:" << baseAddress
                     << "width:" << width
                     << "height:" << height
                     << "bytesPerRow:" << bytesPerRow;
        }

        if (!baseAddress || width == 0 || height == 0) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - Invalid buffer data";
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            return;
        }

        // Create QImage from pixel buffer (BGRA format)
        QImage fullImage(
            static_cast<uchar *>(baseAddress),
            static_cast<int>(width),
            static_cast<int>(height),
            static_cast<int>(bytesPerRow),
            QImage::Format_ARGB32
        );

        // Double-check engine is still valid before calling (defense in depth)
        if (!_invalidated && localEngine) {
            localEngine->setLatestFrame(fullImage.copy());
        }

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        if (frameCount <= 5) {
            qDebug() << "SCKStreamDelegate::didOutputSampleBuffer - Frame processed successfully";
        }
    } @finally {
        // Always executed - mark callback as complete
        dispatch_group_leave(_processingGroup);
    }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    if (error) {
        NSLog(@"SCKCaptureEngine: Stream stopped with error: %@", error.localizedDescription);
    }
}

@end
#endif

void SCKCaptureEngine::Private::cleanup()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        // Step 1: Mark as invalidated to prevent new frame processing
        if (delegate) {
            delegate.invalidated = YES;
            delegate.engine = nil;
        }

        // Step 2: Wait for all in-progress callbacks to complete (max 3 seconds)
        if (delegate && delegate.processingGroup) {
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC);
            long result = dispatch_group_wait(delegate.processingGroup, timeout);
            if (result != 0) {
                qWarning() << "SCKCaptureEngine::cleanup - Timeout waiting for callbacks to complete";
            }
        }

        // Step 3: Stop the stream
        if (stream) {
            dispatch_semaphore_t stopSem = dispatch_semaphore_create(0);
            [stream stopCaptureWithCompletionHandler:^(NSError *error) {
                if (error) {
                    NSLog(@"SCKCaptureEngine: stopCapture error: %@", error.localizedDescription);
                }
                dispatch_semaphore_signal(stopSem);
            }];
            dispatch_semaphore_wait(stopSem, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
            stream = nil;
        }

        // Step 4: Clean up delegate (all callbacks are now complete)
        delegate = nil;
        targetDisplay = nil;
    }
#endif
    running = false;
}

// SCKCaptureEngine implementation

SCKCaptureEngine::SCKCaptureEngine(QObject *parent)
    : ICaptureEngine(parent)
    , d(new Private)
{
}

SCKCaptureEngine::~SCKCaptureEngine()
{
    stop();
    delete d;
}

bool SCKCaptureEngine::isAvailable()
{
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        return true;
    }
#endif
    return false;
}

bool SCKCaptureEngine::hasScreenRecordingPermission()
{
    // On macOS 12.3+, use ScreenCaptureKit's permission check
#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        // Try to get shareable content - will fail if no permission
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block BOOL hasPermission = NO;

        [SCShareableContent getShareableContentWithCompletionHandler:^(
            SCShareableContent *content, NSError *error) {
            hasPermission = (content != nil && error == nil);
            dispatch_semaphore_signal(semaphore);
        }];

        dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
        return hasPermission;
    }
#endif
    return false;
}

bool SCKCaptureEngine::setRegion(const QRect &region, QScreen *screen)
{
    if (!screen) {
        emit error("Invalid screen");
        return false;
    }

    d->captureRegion = region;
    d->targetScreen = screen;
    m_captureRegion = region;
    m_targetScreen = screen;
    return true;
}

void SCKCaptureEngine::setFrameRate(int fps)
{
    m_frameRate = fps;
    d->frameRate = fps;
}

void SCKCaptureEngine::setExcludedWindows(const QList<WId> &windowIds)
{
    d->excludedWindowIds.clear();
    for (WId wid : windowIds) {
        // Convert Qt WId (NSView*) to CGWindowID
        NSView *view = reinterpret_cast<NSView *>(wid);
        if (view) {
            NSWindow *window = [view window];
            if (window) {
                CGWindowID windowId = static_cast<CGWindowID>([window windowNumber]);
                d->excludedWindowIds.append(windowId);
                qDebug() << "SCKCaptureEngine: Added excluded window ID:" << windowId;
            }
        }
    }
}

bool SCKCaptureEngine::start()
{
    qDebug() << "SCKCaptureEngine::start() - BEGIN";
    qDebug() << "SCKCaptureEngine::start() - targetScreen:" << (d->targetScreen ? d->targetScreen->name() : "NULL");
    qDebug() << "SCKCaptureEngine::start() - captureRegion:" << d->captureRegion;

    if (!d->targetScreen || d->captureRegion.isEmpty()) {
        qDebug() << "SCKCaptureEngine::start() - ERROR: Region or screen not configured";
        emit error("Region or screen not configured");
        return false;
    }

#if HAS_SCREENCAPTUREKIT
    if (@available(macOS 12.3, *)) {
        qDebug() << "SCKCaptureEngine::start() - macOS 12.3+ available, using ScreenCaptureKit";
        d->useScreenCaptureKit = true;

        // Get shareable content
        qDebug() << "SCKCaptureEngine::start() - Getting shareable content...";
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block SCShareableContent *content = nil;
        __block NSError *contentError = nil;

        [SCShareableContent getShareableContentWithCompletionHandler:^(
            SCShareableContent *shareableContent, NSError *error) {
            // Retain the content to prevent it from being released
            content = [shareableContent retain];
            contentError = [error retain];
            dispatch_semaphore_signal(semaphore);
        }];

        dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        qDebug() << "SCKCaptureEngine::start() - Shareable content retrieved, content:" << (content ? "valid" : "nil");

        if (!content || contentError) {
            qWarning() << "SCKCaptureEngine: Failed to get shareable content:"
                       << (contentError ? QString::fromNSString(contentError.localizedDescription) : "timeout");
            d->useScreenCaptureKit = false;
            emit error("Failed to get screen content. Check screen recording permission.");
            [content release];
            [contentError release];
            return false;
        }

        qDebug() << "SCKCaptureEngine::start() - Number of displays:" << content.displays.count;
        qDebug() << "SCKCaptureEngine::start() - Number of windows:" << content.windows.count;

        @try {
            // Find the display matching our target screen
            CGDirectDisplayID targetDisplayID = 0;

            qDebug() << "SCKCaptureEngine::start() - Finding target display...";
            qDebug() << "SCKCaptureEngine::start() - Target screen name:" << d->targetScreen->name();
            qDebug() << "SCKCaptureEngine::start() - Target screen geometry:" << d->targetScreen->geometry();

            // Match display by geometry position instead of name (more reliable)
            // NSScreen.localizedName uses localized strings which may not match QScreen::name()
            QRect targetGeom = d->targetScreen->geometry();
            qDebug() << "SCKCaptureEngine::start() - Matching by geometry:" << targetGeom;

            // Get all active displays and match by position
            uint32_t displayCount = 0;
            CGDirectDisplayID displays[16];
            CGGetActiveDisplayList(16, displays, &displayCount);

            qDebug() << "SCKCaptureEngine::start() - Active displays:" << displayCount;

            for (uint32_t i = 0; i < displayCount; i++) {
                CGRect displayBounds = CGDisplayBounds(displays[i]);
                qDebug() << "SCKCaptureEngine::start() - Display" << i << "ID:" << displays[i]
                         << "bounds: x=" << displayBounds.origin.x
                         << "y=" << displayBounds.origin.y
                         << "w=" << displayBounds.size.width
                         << "h=" << displayBounds.size.height;

                // CGDisplayBounds returns logical coordinates, same as QScreen::geometry()
                if (static_cast<int>(displayBounds.origin.x) == targetGeom.x() &&
                    static_cast<int>(displayBounds.origin.y) == targetGeom.y() &&
                    static_cast<int>(displayBounds.size.width) == targetGeom.width() &&
                    static_cast<int>(displayBounds.size.height) == targetGeom.height()) {
                    targetDisplayID = displays[i];
                    qDebug() << "SCKCaptureEngine::start() - Matched display by geometry, ID:" << targetDisplayID;
                    break;
                }
            }

            if (targetDisplayID == 0) {
                qWarning() << "SCKCaptureEngine: Could not match screen by geometry, using main display";
                targetDisplayID = CGMainDisplayID();
                qDebug() << "SCKCaptureEngine::start() - Using main display ID:" << targetDisplayID;
                // Emit warning to user about fallback
                emit warning(tr("Could not match the selected screen. Recording from main display instead."));
            }

            // Find SCDisplay matching the target
            qDebug() << "SCKCaptureEngine::start() - Searching for SCDisplay with ID:" << targetDisplayID;
            for (SCDisplay *display in content.displays) {
                qDebug() << "SCKCaptureEngine::start() - SCDisplay ID:" << display.displayID
                         << "width:" << display.width << "height:" << display.height;
                if (display.displayID == targetDisplayID) {
                    d->targetDisplay = display;
                    qDebug() << "SCKCaptureEngine::start() - Found matching SCDisplay";
                    break;
                }
            }

            if (!d->targetDisplay && content.displays.count > 0) {
                d->targetDisplay = content.displays.firstObject;
                qDebug() << "SCKCaptureEngine::start() - Using first available display as fallback";
            }

            if (!d->targetDisplay) {
                qWarning() << "SCKCaptureEngine: No display found";
                d->useScreenCaptureKit = false;
                emit error("No display found for capture");
                [content release];
                return false;
            }

            qDebug() << "SCKCaptureEngine::start() - Creating content filter...";

            // Build list of windows to exclude from capture
            NSMutableArray<SCWindow *> *excludedWindows = [NSMutableArray array];
            if (!d->excludedWindowIds.isEmpty()) {
                qDebug() << "SCKCaptureEngine::start() - Looking for" << d->excludedWindowIds.size() << "windows to exclude";
                for (SCWindow *window in content.windows) {
                    CGWindowID windowId = window.windowID;
                    if (d->excludedWindowIds.contains(windowId)) {
                        [excludedWindows addObject:window];
                        qDebug() << "SCKCaptureEngine::start() - Excluding window ID:" << windowId
                                 << "title:" << QString::fromNSString(window.title);
                    }
                }
                qDebug() << "SCKCaptureEngine::start() - Found" << excludedWindows.count << "windows to exclude";
            }

            // Create content filter for the display, excluding our UI windows
            SCContentFilter *filter = [[SCContentFilter alloc]
                initWithDisplay:d->targetDisplay
                excludingWindows:excludedWindows];
            qDebug() << "SCKCaptureEngine::start() - Content filter created:" << (filter ? "valid" : "nil");

            // Get device pixel ratio for output scaling
            CGFloat scale = d->targetScreen->devicePixelRatio();
            qDebug() << "SCKCaptureEngine::start() - Device pixel ratio:" << scale;

            // Configure stream
            qDebug() << "SCKCaptureEngine::start() - Creating stream configuration...";
            SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];

            // Output dimensions in physical pixels (for Retina quality)
            size_t outputWidth = static_cast<size_t>(d->captureRegion.width() * scale);
            size_t outputHeight = static_cast<size_t>(d->captureRegion.height() * scale);
            qDebug() << "SCKCaptureEngine::start() - Output dimensions:" << outputWidth << "x" << outputHeight;

            config.width = outputWidth;
            config.height = outputHeight;
            config.minimumFrameInterval = CMTimeMake(1, d->frameRate);
            config.showsCursor = NO;
            config.pixelFormat = kCVPixelFormatType_32BGRA;
            qDebug() << "SCKCaptureEngine::start() - Frame rate:" << d->frameRate;

            // sourceRect is in POINT coordinates (logical pixels), not physical pixels
            // It defines which region of the display to capture
            // ScreenCaptureKit uses top-left origin (same as Qt), no Y flip needed
            CGFloat relativeX = d->captureRegion.x() - d->targetScreen->geometry().x();
            CGFloat relativeY = d->captureRegion.y() - d->targetScreen->geometry().y();

            qDebug() << "=== SCKCaptureEngine Coordinate Debug ===";
            qDebug() << "Qt screen geometry:" << d->targetScreen->geometry();
            qDebug() << "SCDisplay size:" << d->targetDisplay.width << "x" << d->targetDisplay.height;
            qDebug() << "Device pixel ratio:" << scale;
            qDebug() << "captureRegion (global):" << d->captureRegion;
            qDebug() << "relativeX:" << relativeX << "relativeY:" << relativeY;

            CGRect sourceRect = CGRectMake(
                relativeX,
                relativeY,
                d->captureRegion.width(),
                d->captureRegion.height()
            );
            config.sourceRect = sourceRect;

            qDebug() << "sourceRect: x=" << sourceRect.origin.x
                     << "y=" << sourceRect.origin.y
                     << "w=" << sourceRect.size.width
                     << "h=" << sourceRect.size.height;
            qDebug() << "=== End Coordinate Debug ===";

            // Create stream
            qDebug() << "SCKCaptureEngine::start() - Creating delegate...";
            d->delegate = [[SCKStreamDelegate alloc] init];
            d->delegate.engine = d;
            d->delegate.invalidated = NO;
            qDebug() << "SCKCaptureEngine::start() - Delegate created:" << (d->delegate ? "valid" : "nil");

            qDebug() << "SCKCaptureEngine::start() - Creating SCStream...";
            d->stream = [[SCStream alloc] initWithFilter:filter
                                           configuration:config
                                                delegate:d->delegate];
            qDebug() << "SCKCaptureEngine::start() - SCStream created:" << (d->stream ? "valid" : "nil");

            qDebug() << "SCKCaptureEngine::start() - Adding stream output...";
            NSError *addOutputError = nil;
            BOOL success = [d->stream addStreamOutput:d->delegate
                                                 type:SCStreamOutputTypeScreen
                                   sampleHandlerQueue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)
                                                error:&addOutputError];
            qDebug() << "SCKCaptureEngine::start() - addStreamOutput result:" << (success ? "YES" : "NO");

            if (!success) {
                qWarning() << "SCKCaptureEngine: Failed to add stream output:"
                           << QString::fromNSString(addOutputError.localizedDescription);
                d->useScreenCaptureKit = false;
                emit error("Failed to initialize capture stream");
                [content release];
                return false;
            }

            qDebug() << "SCKCaptureEngine::start() - Starting capture...";
            dispatch_semaphore_t startSemaphore = dispatch_semaphore_create(0);
            __block NSError *startError = nil;

            [d->stream startCaptureWithCompletionHandler:^(NSError *error) {
                if (error) {
                    NSLog(@"SCKCaptureEngine: startCapture error: %@", error.localizedDescription);
                }
                startError = error;
                dispatch_semaphore_signal(startSemaphore);
            }];

            qDebug() << "SCKCaptureEngine::start() - Waiting for capture to start...";
            dispatch_semaphore_wait(startSemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
            qDebug() << "SCKCaptureEngine::start() - Capture start completed, error:" << (startError ? "YES" : "NO");

            if (startError) {
                qWarning() << "SCKCaptureEngine: Failed to start capture:"
                           << QString::fromNSString(startError.localizedDescription);
                d->useScreenCaptureKit = false;
                emit error("Failed to start capture: " + QString::fromNSString(startError.localizedDescription));
                [content release];
                return false;
            }

            // Release the retained content after successful setup
            [content release];

            qDebug() << "SCKCaptureEngine::start() - Capture started successfully";
        } @catch (NSException *e) {
            qWarning() << "SCKCaptureEngine: Exception during initialization:"
                       << QString::fromNSString(e.name)
                       << QString::fromNSString(e.reason);
            d->useScreenCaptureKit = false;
            emit error("Exception during capture initialization: " + QString::fromNSString(e.reason));
            [content release];
            return false;
        }
    } else {
        qDebug() << "SCKCaptureEngine::start() - macOS 12.3+ NOT available";
        emit error("ScreenCaptureKit requires macOS 12.3 or later");
        return false;
    }
#else
    qDebug() << "SCKCaptureEngine::start() - HAS_SCREENCAPTUREKIT=0";
    emit error("ScreenCaptureKit not available on this system");
    return false;
#endif

    d->running = true;
    qDebug() << "SCKCaptureEngine: Started (ScreenCaptureKit)"
             << "region:" << d->captureRegion;
    qDebug() << "SCKCaptureEngine::start() - END (success)";

    return true;
}

void SCKCaptureEngine::stop()
{
    if (d->running) {
        d->cleanup();
        qDebug() << "SCKCaptureEngine: Stopped";
    }
}

bool SCKCaptureEngine::isRunning() const
{
    return d->running;
}

QImage SCKCaptureEngine::captureFrame()
{
    static int callCount = 0;
    callCount++;

    if (callCount <= 5 || callCount % 30 == 0) {
        qDebug() << "SCKCaptureEngine::captureFrame() - call:" << callCount
                 << "running:" << d->running
                 << "useScreenCaptureKit:" << d->useScreenCaptureKit;
    }

    if (!d->running) {
        if (callCount <= 5) {
            qDebug() << "SCKCaptureEngine::captureFrame() - Not running, returning empty";
        }
        return QImage();
    }

#if HAS_SCREENCAPTUREKIT
    if (d->useScreenCaptureKit) {
        // Return the latest frame from async capture
        QImage frame = d->getLatestFrame();
        if (callCount <= 5) {
            qDebug() << "SCKCaptureEngine::captureFrame() - Frame size:" << frame.size()
                     << "isNull:" << frame.isNull();
        }
        return frame;
    }
#endif

    if (callCount <= 5) {
        qDebug() << "SCKCaptureEngine::captureFrame() - Fallback, returning empty";
    }
    return QImage();
}

QString SCKCaptureEngine::engineName() const
{
    return QStringLiteral("ScreenCaptureKit");
}
