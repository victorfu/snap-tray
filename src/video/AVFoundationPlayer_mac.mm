#include "video/IVideoPlayer.h"

#ifdef Q_OS_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include <QDebug>
#include <QImage>
#include <QTimer>
#include <QUrl>

// Forward declaration
class AVFoundationPlayer;

// Objective-C helper class for KVO and notifications
@interface AVFoundationPlayerHelper : NSObject
@property (nonatomic, assign) AVFoundationPlayer *player;
@property (nonatomic, strong) AVPlayer *avPlayer;
@property (nonatomic, strong) AVPlayerItemVideoOutput *videoOutput;
@property (nonatomic, strong) id timeObserver;
- (void)setupWithAVPlayer:(AVPlayer *)avPlayer;
- (void)cleanup;
- (QImage)currentFrame;
@end

class AVFoundationPlayer : public IVideoPlayer
{
    Q_OBJECT

public:
    explicit AVFoundationPlayer(QObject *parent = nullptr);
    ~AVFoundationPlayer() override;

    // IVideoPlayer interface
    bool load(const QString &filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;

    State state() const override { return m_state; }
    qint64 duration() const override { return m_duration; }
    qint64 position() const override { return m_position; }
    QSize videoSize() const override { return m_videoSize; }
    bool hasVideo() const override { return m_hasVideo; }

    void setVolume(float volume) override;
    float volume() const override { return m_volume; }
    void setMuted(bool muted) override;
    bool isMuted() const override { return m_muted; }

    void setLooping(bool loop) override { m_looping = loop; }
    bool isLooping() const override { return m_looping; }

    void setPlaybackRate(float rate) override;
    float playbackRate() const override { return m_playbackRate; }

    double frameRate() const override { return m_frameIntervalMs > 0 ? 1000.0 / m_frameIntervalMs : 30.0; }
    int frameIntervalMs() const override { return m_frameIntervalMs; }

    // Called from Objective-C helper
    void onStatusChanged(int status);
    void onTimeUpdate(qint64 timeMs);
    void onPlaybackFinished();

private:
    void updateFrame();
    void setState(State newState);

    AVFoundationPlayerHelper *m_helper;
    QTimer *m_frameTimer;

    State m_state;
    qint64 m_duration;
    qint64 m_position;
    QSize m_videoSize;
    bool m_hasVideo;
    float m_volume;
    bool m_muted;
    bool m_looping;
    float m_playbackRate;
    int m_frameIntervalMs;
};

@implementation AVFoundationPlayerHelper

- (void)setupWithAVPlayer:(AVPlayer *)avPlayer
{
    self.avPlayer = avPlayer;

    // Create video output for frame extraction
    NSDictionary *pixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
    };
    self.videoOutput = [[AVPlayerItemVideoOutput alloc]
        initWithPixelBufferAttributes:pixelBufferAttributes];

    // Add observer for player item status
    [avPlayer.currentItem addObserver:self
                           forKeyPath:@"status"
                              options:NSKeyValueObservingOptionNew
                              context:nil];

    // Add observer for playback finished
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(playerItemDidReachEnd:)
               name:AVPlayerItemDidPlayToEndTimeNotification
             object:avPlayer.currentItem];

    // Add periodic time observer
    __weak AVFoundationPlayerHelper *weakSelf = self;
    CMTime interval = CMTimeMakeWithSeconds(0.1, NSEC_PER_SEC);
    self.timeObserver = [avPlayer addPeriodicTimeObserverForInterval:interval
                                                               queue:dispatch_get_main_queue()
                                                          usingBlock:^(CMTime time) {
        AVFoundationPlayerHelper *strongSelf = weakSelf;
        if (strongSelf && strongSelf.player) {
            qint64 timeMs = (qint64)(CMTimeGetSeconds(time) * 1000.0);
            strongSelf.player->onTimeUpdate(timeMs);
        }
    }];

    // Add video output to player item
    [avPlayer.currentItem addOutput:self.videoOutput];
}

- (void)cleanup
{
    if (self.avPlayer) {
        if (self.timeObserver) {
            [self.avPlayer removeTimeObserver:self.timeObserver];
            self.timeObserver = nil;
        }

        if (self.avPlayer.currentItem) {
            @try {
                [self.avPlayer.currentItem removeObserver:self forKeyPath:@"status"];
            } @catch (NSException *exception) {
                // Observer might not be registered
            }
            [self.avPlayer.currentItem removeOutput:self.videoOutput];
        }

        [[NSNotificationCenter defaultCenter] removeObserver:self];
    }

    self.avPlayer = nil;
    self.videoOutput = nil;
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey,id> *)change
                       context:(void *)context
{
    if ([keyPath isEqualToString:@"status"] && self.player) {
        AVPlayerItemStatus status = (AVPlayerItemStatus)[change[NSKeyValueChangeNewKey] integerValue];
        self.player->onStatusChanged((int)status);
    }
}

- (void)playerItemDidReachEnd:(NSNotification *)notification
{
    if (self.player) {
        self.player->onPlaybackFinished();
    }
}

- (QImage)currentFrame
{
    if (!self.videoOutput || !self.avPlayer.currentItem) {
        return QImage();
    }

    CMTime currentTime = self.avPlayer.currentItem.currentTime;
    
    // Note: We intentionally DO NOT check hasNewPixelBufferForItemTime here
    // because that only works during playback. For seeking/frame extraction,
    // we want to get the frame directly at the current time.
    CVPixelBufferRef pixelBuffer = [self.videoOutput copyPixelBufferForItemTime:currentTime
                                                             itemTimeForDisplay:nil];
    if (!pixelBuffer) {
        return QImage();
    }

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);

    // Create QImage from pixel buffer (BGRA format)
    QImage image((const uchar *)baseAddress, (int)width, (int)height,
                 (int)bytesPerRow, QImage::Format_ARGB32);
    QImage result = image.copy();  // Deep copy before unlocking

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(pixelBuffer);

    return result;
}

@end

// AVFoundationPlayer implementation
AVFoundationPlayer::AVFoundationPlayer(QObject *parent)
    : IVideoPlayer(parent)
    , m_helper([[AVFoundationPlayerHelper alloc] init])
    , m_frameTimer(new QTimer(this))
    , m_state(State::Stopped)
    , m_duration(0)
    , m_position(0)
    , m_hasVideo(false)
    , m_volume(1.0f)
    , m_muted(false)
    , m_looping(false)
    , m_playbackRate(1.0f)
    , m_frameIntervalMs(33)
{
    m_helper.player = this;

    // Frame update timer - interval will be updated when video loads
    connect(m_frameTimer, &QTimer::timeout, this, &AVFoundationPlayer::updateFrame);
    m_frameTimer->setInterval(m_frameIntervalMs);
}

AVFoundationPlayer::~AVFoundationPlayer()
{
    m_frameTimer->stop();
    [m_helper cleanup];
    m_helper = nil;
}

bool AVFoundationPlayer::load(const QString &filePath)
{
    // Cleanup previous player
    [m_helper cleanup];
    m_hasVideo = false;
    m_duration = 0;
    m_position = 0;
    m_videoSize = QSize();

    // Create URL from file path
    NSURL *url;
    if (filePath.startsWith("file://")) {
        url = [NSURL URLWithString:filePath.toNSString()];
    } else {
        url = [NSURL fileURLWithPath:filePath.toNSString()];
    }

    if (!url) {
        emit error("Invalid file path");
        return false;
    }

    // Create AVPlayer
    AVPlayer *avPlayer = [AVPlayer playerWithURL:url];
    if (!avPlayer) {
        emit error("Failed to create AVPlayer");
        return false;
    }

    [m_helper setupWithAVPlayer:avPlayer];

    qDebug() << "AVFoundationPlayer: Loading" << filePath;
    return true;
}

void AVFoundationPlayer::play()
{
    if (!m_helper.avPlayer) return;

    [m_helper.avPlayer setRate:m_playbackRate];
    setState(State::Playing);
    m_frameTimer->start();
}

void AVFoundationPlayer::pause()
{
    if (!m_helper.avPlayer) return;

    [m_helper.avPlayer pause];
    setState(State::Paused);
    m_frameTimer->stop();
}

void AVFoundationPlayer::stop()
{
    if (!m_helper.avPlayer) return;

    [m_helper.avPlayer pause];
    [m_helper.avPlayer seekToTime:kCMTimeZero];
    setState(State::Stopped);
    m_frameTimer->stop();
    m_position = 0;
    emit positionChanged(0);
}

void AVFoundationPlayer::seek(qint64 positionMs)
{
    if (!m_helper.avPlayer) return;

    CMTime time = CMTimeMakeWithSeconds(positionMs / 1000.0, NSEC_PER_SEC);

    // Use completion handler to update frame after seek completes
    __weak AVFoundationPlayerHelper *weakHelper = m_helper;
    AVFoundationPlayer *player = this;

    [m_helper.avPlayer seekToTime:time
                  toleranceBefore:kCMTimeZero
                   toleranceAfter:kCMTimeZero
                completionHandler:^(BOOL finished) {
        if (finished && player->state() != State::Playing) {
            // Fetch and emit frame when paused
            dispatch_async(dispatch_get_main_queue(), ^{
                AVFoundationPlayerHelper *strongHelper = weakHelper;
                if (strongHelper) {
                    QImage frame = [strongHelper currentFrame];
                    // Always emit frameReady so downstream consumers (like VideoTrimmer)
                    // don't hang waiting for a signal that never comes.
                    // Consumers should handle null frames appropriately.
                    emit player->frameReady(frame);
                }
            });
        }
    }];
}

void AVFoundationPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_helper.avPlayer && !m_muted) {
        m_helper.avPlayer.volume = m_volume;
    }
}

void AVFoundationPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_helper.avPlayer) {
        m_helper.avPlayer.volume = muted ? 0.0f : m_volume;
    }
}

void AVFoundationPlayer::setPlaybackRate(float rate)
{
    float newRate = qBound(0.25f, rate, 2.0f);
    if (m_playbackRate != newRate) {
        m_playbackRate = newRate;
        if (m_helper.avPlayer && m_state == State::Playing) {
            [m_helper.avPlayer setRate:m_playbackRate];
        }
        emit playbackRateChanged(m_playbackRate);
    }
}

void AVFoundationPlayer::onStatusChanged(int status)
{
    if (status == AVPlayerItemStatusReadyToPlay) {
        AVPlayerItem *item = m_helper.avPlayer.currentItem;
        if (item) {
            // Get duration
            CMTime durationTime = item.duration;
            if (CMTIME_IS_VALID(durationTime)) {
                m_duration = (qint64)(CMTimeGetSeconds(durationTime) * 1000.0);
                emit durationChanged(m_duration);
            }

            // Get video size and frame rate
            NSArray *tracks = item.asset.tracks;
            for (AVAssetTrack *track in tracks) {
                if ([track.mediaType isEqualToString:AVMediaTypeVideo]) {
                    m_hasVideo = true;
                    CGSize size = track.naturalSize;
                    CGAffineTransform transform = track.preferredTransform;

                    // Apply rotation to size if needed
                    if (transform.a == 0 && transform.d == 0) {
                        // 90 or 270 degree rotation
                        m_videoSize = QSize((int)size.height, (int)size.width);
                    } else {
                        m_videoSize = QSize((int)size.width, (int)size.height);
                    }

                    // Extract frame rate from video track
                    float fps = track.nominalFrameRate;
                    if (fps > 0) {
                        m_frameIntervalMs = static_cast<int>(1000.0 / fps);
                        m_frameTimer->setInterval(m_frameIntervalMs);
                    }
                    break;
                }
            }

            qDebug() << "AVFoundationPlayer: Media loaded, duration:" << m_duration
                     << "size:" << m_videoSize << "fps:" << frameRate();
            emit mediaLoaded();
        }
    } else if (status == AVPlayerItemStatusFailed) {
        NSError *error = m_helper.avPlayer.currentItem.error;
        QString errorMsg = error ? QString::fromNSString(error.localizedDescription)
                                 : "Unknown error";
        emit this->error(errorMsg);
    }
}

void AVFoundationPlayer::onTimeUpdate(qint64 timeMs)
{
    if (m_position != timeMs) {
        m_position = timeMs;
        emit positionChanged(timeMs);
    }
}

void AVFoundationPlayer::onPlaybackFinished()
{
    if (m_looping) {
        seek(0);
        play();
    } else {
        setState(State::Stopped);
        m_frameTimer->stop();
        emit playbackFinished();
    }
}

void AVFoundationPlayer::updateFrame()
{
    if (m_state != State::Playing) return;

    QImage frame = [m_helper currentFrame];
    if (!frame.isNull()) {
        emit frameReady(frame);
    }
}

void AVFoundationPlayer::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

// Factory function implementations
IVideoPlayer* createAVFoundationPlayer(QObject *parent)
{
    return new AVFoundationPlayer(parent);
}

bool isAVFoundationAvailable()
{
    // AVFoundation is available on macOS 10.7+
    return true;
}

#include "AVFoundationPlayer_mac.moc"

#endif // Q_OS_MAC
