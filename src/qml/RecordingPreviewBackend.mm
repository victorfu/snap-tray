#include "qml/RecordingPreviewBackend.h"
#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"
#include "utils/VideoCropGeometry.h"
#include "video/IVideoTranscoder.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoPlayer.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "settings/RecordingSettingsManager.h"
#include "recording/WindowTimelineSidecar.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlContext>
#include <QQuickView>
#include <QTimer>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

#ifdef Q_OS_WIN
#include <objbase.h>
#endif

namespace {
constexpr int kVideoLoadTimeoutMs = 5000;
// How long an animated export waits for each extracted frame to arrive.
constexpr int kFrameExtractionTimeoutMs = 5000;
}

RecordingPreviewBackend::RecordingPreviewBackend(const QString &videoPath, QObject *parent)
    : RecordingPreviewBackend(videoPath, true, parent)
{
}

RecordingPreviewBackend::RecordingPreviewBackend(const QString &videoPath, bool recordedAsIntermediate,
                                                 QObject *parent)
    : QObject(parent)
    , m_videoPath(videoPath)
    , m_recordedAsIntermediate(recordedAsIntermediate)
{
    m_windowTimeline = SnapTray::WindowTimelineSidecar::read(videoPath);
}

std::function<int()>& RecordingPreviewBackend::outputQualityOverride()
{
    static std::function<int()> override;
    return override;
}

RecordingPreviewBackend::TranscoderFactory& RecordingPreviewBackend::transcoderFactoryOverride()
{
    static TranscoderFactory factory;
    return factory;
}

RecordingPreviewBackend::~RecordingPreviewBackend()
{
    m_exportCancelToken->store(true);

    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        // Disconnect to prevent re-entrant signals during teardown
        disconnect(m_view, nullptr, this, nullptr);
        m_view->close();
        // Delete synchronously so the QML tree is destroyed before this QObject's
        // destructor notifies QML that the "backend" context property is gone.
        // Do NOT call setContextProperty(nullptr) before delete; that would trigger
        // QML binding re-evaluation while the tree still exists, causing null-reference errors.
        delete m_view;
        m_view = nullptr;
    }
}

void RecordingPreviewBackend::ensureView()
{
    if (m_view)
        return;

    auto& mgr = SnapTray::QmlOverlayManager::instance();
    m_view = mgr.createUtilityWindow();
#ifdef Q_OS_WIN
    // Qt 6.11's Windows backend turns a plain Qt::Window that carries
    // WindowStaysOnTopHint into a caption-less WS_POPUP frame; spelling out
    // the caption hints keeps the title bar and its min/max/close buttons.
    m_view->setFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint
                     | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                     | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
#else
    m_view->setFlag(Qt::WindowStaysOnTopHint, true);
#endif
    m_view->setMinimumSize(QSize(640, 480));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(tr("Recording Preview"));
    m_view->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("RecordingPreviewBackend"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("RecordingPreviewBackend"));

    // Set backend as context property before loading QML
    m_view->rootContext()->setContextProperty(
        QStringLiteral("backend"), this);

    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingPreview.qml")));

    // Handle window close
    connect(m_view, &QQuickView::closing, this, [this](QQuickCloseEvent *) {
        finishClose();
    });
}

bool RecordingPreviewBackend::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event && event->type() == QEvent::Close && m_isProcessing) {
        static_cast<QCloseEvent*>(event)->ignore();
        return true;
    }

    if (watched == m_view && event) {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close) {
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        } else if (CursorSurfaceSupport::isPointerRefreshEvent(event->type())) {
            syncCursorSurface();
        }
    }

    return QObject::eventFilter(watched, event);
}

void RecordingPreviewBackend::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView *nsView = reinterpret_cast<NSView *>(m_view->winId());
    if (!nsView)
        return;

    NSWindow *nsWindow = [nsView window];
    if (!nsWindow)
        return;

    [nsWindow setHidesOnDeactivate:NO];
#endif
}

void RecordingPreviewBackend::show()
{
    ensureView();

    // Center on primary screen
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int w = 1024, h = 768;
        int x = screenGeometry.center().x() - w / 2;
        int y = screenGeometry.center().y() - h / 2;
        m_view->setGeometry(x, y, w, h);
    } else {
        m_view->resize(1024, 768);
    }

    m_view->show();
    applyPlatformWindowFlags();
    m_view->raise();
    m_view->requestActivate();
    syncCursorSurface();
}

void RecordingPreviewBackend::close()
{
    if (isProcessing() || m_closeHandled) return;
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
    } else {
        finishClose();
    }
}

void RecordingPreviewBackend::finishClose()
{
    if (m_closeHandled) return;
    m_closeHandled = true;
    const QString discardPath = m_videoPath;
    const bool saved = m_saved;
    // Preserve teardown ordering: the owner schedules deletion of playback
    // resources before the queued discard handler removes the temporary file.
    emit closed(saved);
    if (!saved) emit discardRequested(discardPath);
}

void RecordingPreviewBackend::syncCursorSurface()
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::SurfaceDefault);
}

void RecordingPreviewBackend::setDefaultOutputFormat(int formatInt)
{
    int format = qBound(0, formatInt, 2);
    setSelectedFormat(format);
}

// ---------- Property setters ----------

void RecordingPreviewBackend::setTrimStart(qint64 ms)
{
    ms = qBound(qint64(0), ms, m_duration);
    if (m_trimStart != ms) {
        m_trimStart = ms;
        emit trimRangeChanged();
    }
}

void RecordingPreviewBackend::setTrimEnd(qint64 ms)
{
    if (ms < 0)
        ms = -1;
    else
        ms = qMax(ms, m_trimStart + 1);
    if (m_trimEnd != ms) {
        m_trimEnd = ms;
        emit trimRangeChanged();
    }
}

qint64 RecordingPreviewBackend::trimEnd() const
{
    return (m_trimEnd < 0) ? m_duration : m_trimEnd;
}

bool RecordingPreviewBackend::hasTrim() const
{
    return m_trimStart > 0 || (m_trimEnd >= 0 && m_trimEnd < m_duration);
}

qint64 RecordingPreviewBackend::trimmedDuration() const
{
    return trimEnd() - m_trimStart;
}

void RecordingPreviewBackend::setSelectedFormat(int format)
{
    if (isProcessing()) return;
    format = qBound(0, format, 2);
    auto fmt = static_cast<OutputFormat>(format);
    if (m_selectedFormat != fmt) {
        m_selectedFormat = fmt;
        emit formatChanged();
    }
}

void RecordingPreviewBackend::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

void RecordingPreviewBackend::clearError()
{
    setErrorMessage(QString());
}

void RecordingPreviewBackend::reportPlaybackError(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }

    setErrorMessage(tr("Failed to load preview video: %1").arg(message));
}

// ---------- QML state updates ----------
void RecordingPreviewBackend::updatePosition(qint64 ms)
{
    if (m_position != ms) {
        m_position = ms;
        emit positionChanged();
    }
}

void RecordingPreviewBackend::updateDuration(qint64 ms)
{
    if (m_duration != ms) {
        m_duration = ms;
        // If trimEnd was -1 (unset), it now means the full duration
        emit durationChanged();
        emit trimRangeChanged();
    }
}

void RecordingPreviewBackend::updatePlayingState(bool playing)
{
    if (m_isPlaying != playing) {
        m_isPlaying = playing;
        emit stateChanged();
    }
}

// ---------- Actions ----------

QString RecordingPreviewBackend::formatTime(qint64 ms) const
{
    int totalSeconds = static_cast<int>(ms / 1000);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void RecordingPreviewBackend::save()
{
    qDebug() << "RecordingPreviewBackend: Save requested";
    if (isProcessing() || m_closeHandled) {
        qDebug() << "RecordingPreviewBackend: Save ignored while processing";
        return;
    }

    // Output quality contract: snapshot the user's setting when Save begins;
    // every MP4 re-encode below uses it. Quality 80 is only the legacy default.
    m_outputQuality = outputQualityOverride() ? outputQualityOverride()()
                                              : RecordingSettingsManager::instance().quality();

    // Animated exports share the same offline extraction path, including trims.
    if (m_selectedFormat != MP4) {
        performFormatConversion(m_selectedFormat);
        return;
    }

    if (hasTrim() || hasCrop()) {
        performTranscode(false);
        return;
    }

    // MP4 with no edits: move the intermediate if it already meets the
    // user's quality, otherwise re-encode it to that quality.
    performTranscode(true);
}

bool RecordingPreviewBackend::canCancelExport() const
{
    return m_isProcessing && m_exportKind == ExportKind::MP4 && !m_exportCancelToken->load();
}

void RecordingPreviewBackend::finishProcessing()
{
    m_exportKind = ExportKind::None;
    m_isProcessing = false;
    emit processingChanged();
}

void RecordingPreviewBackend::cancelExport()
{
    if (!canCancelExport()) {
        return;
    }
    qDebug() << "RecordingPreviewBackend: Export cancel requested";
    m_exportCancelToken->store(true);
    m_processStatus = tr("Cancelling...");
    emit processStatusChanged();
    emit processingChanged();
}

void RecordingPreviewBackend::discard()
{
    qDebug() << "RecordingPreviewBackend: Discard requested";
    if (isProcessing() || m_closeHandled) {
        qDebug() << "RecordingPreviewBackend: Discard ignored while processing";
        return;
    }
    m_saved = false;
    close(); // All close paths share the same discard/teardown notification.
}

void RecordingPreviewBackend::toggleTrim()
{
    constexpr qint64 kMinTrimSpanMs = 100;

    if (m_duration <= kMinTrimSpanMs) {
        return;
    }

    if (hasTrim()) {
        bool changed = false;
        if (m_trimStart != 0) {
            m_trimStart = 0;
            changed = true;
        }
        if (m_trimEnd != -1) {
            m_trimEnd = -1;
            changed = true;
        }
        if (changed) {
            emit trimRangeChanged();
        }
        return;
    }

    // Establish an initial editable trim region so handles become available.
    qint64 marginMs = qMin<qint64>(1000, m_duration / 10);
    qint64 start = marginMs;
    qint64 end = m_duration - marginMs;

    if (end - start < kMinTrimSpanMs) {
        start = 0;
        end = m_duration - 1;
    }

    if (end <= start || end >= m_duration) {
        return;
    }

    m_trimStart = start;
    m_trimEnd = end;
    emit trimRangeChanged();
}

// ---------- Crop ----------

void RecordingPreviewBackend::updateVideoSize(const QSize &size)
{
    if (m_videoSize == size) {
        return;
    }
    m_videoSize = size;
    emit videoSizeChanged();
    if (m_windowTimeline && m_windowTimeline->frameSize() != size) {
        qWarning() << "RecordingPreviewBackend: window timeline frame size" << m_windowTimeline->frameSize()
                   << "does not match the video" << size << "- snapping disabled";
        m_windowTimeline.reset();
        emit windowTimelineChanged();
    }
    setCropRect(m_cropRect);
}

void RecordingPreviewBackend::setCropRect(const QRect &videoRect)
{
    const QRect normalized = SnapTray::VideoCropGeometry::normalizeCropRect(videoRect, m_videoSize);
    if (normalized == m_cropRect) {
        return;
    }
    m_cropRect = normalized;
    emit cropRectChanged();
}

void RecordingPreviewBackend::setCropFromView(const QRectF &viewRect, const QRectF &contentRect)
{
    setCropRect(SnapTray::VideoCropGeometry::viewToVideo(viewRect, contentRect, m_videoSize));
}

QRectF RecordingPreviewBackend::cropRectInView(const QRectF &contentRect) const
{
    if (!hasCrop()) {
        return contentRect;
    }
    return SnapTray::VideoCropGeometry::videoToView(m_cropRect, contentRect, m_videoSize);
}

std::optional<SnapTray::WindowSample> RecordingPreviewBackend::windowAt(const QPointF &viewPoint,
                                                                         const QRectF &contentRect,
                                                                         qint64 positionMs) const
{
    if (!m_windowTimeline || m_videoSize.isEmpty()) {
        return std::nullopt;
    }
    const QPoint videoPoint = SnapTray::VideoCropGeometry::viewPointToVideo(viewPoint, contentRect, m_videoSize);
    if (videoPoint.x() < 0) {
        return std::nullopt;
    }
    return m_windowTimeline->hitTest(videoPoint, positionMs);
}

QRectF RecordingPreviewBackend::windowRectInViewAt(const QPointF &viewPoint, const QRectF &contentRect,
                                                   qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? SnapTray::VideoCropGeometry::videoToView(window->rect, contentRect, m_videoSize) : QRectF();
}

QString RecordingPreviewBackend::windowAppAt(const QPointF &viewPoint, const QRectF &contentRect,
                                             qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? window->ownerApp : QString();
}

void RecordingPreviewBackend::clearCrop()
{
    if (m_cropRect.isNull()) {
        return;
    }
    m_cropRect = QRect();
    emit cropRectChanged();
}

// ---------- Format conversion (runs on background thread) ----------

void RecordingPreviewBackend::performFormatConversion(OutputFormat format)
{
    QString extension;
    switch (format) {
    case GIF:  extension = ".gif";  break;
    case WebP: extension = ".webp"; break;
    default: return;
    }

    // Create output path
    QString outputPath = m_videoPath;
    int dotIndex = outputPath.lastIndexOf('.');
    if (dotIndex > 0)
        outputPath = outputPath.left(dotIndex) + extension;
    else
        outputPath += extension;

    // Never overwrite an earlier conversion. A unique final path also makes
    // it impossible to mistake a stale file for this conversion's output.
    if (QFileInfo::exists(outputPath)) {
        const int outputDot = outputPath.lastIndexOf('.');
        const QString suffix = QStringLiteral("_converted_")
            + QString::number(QDateTime::currentMSecsSinceEpoch());
        outputPath = outputDot > 0
            ? outputPath.left(outputDot) + suffix + extension
            : outputPath + suffix + extension;
    }

    qDebug() << "RecordingPreviewBackend: Converting to" << extension;

    // Show processing state
    m_exportKind = ExportKind::Animated;
    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Converting video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    // Capture values needed by the worker thread
    const QString videoPath = m_videoPath;
    const QString sourceVideoPath = m_videoPath;
    const qint64 requestedStartMs = m_trimStart;
    const qint64 requestedEndMs = m_trimEnd;
    const QRect cropRect = m_cropRect;
    const int selectedFormat = static_cast<int>(format);
    const QString createPlayerError = tr("Failed to create video player for conversion");
    const QString loadVideoError = tr("Failed to load video for conversion");
    const QString invalidVideoError = tr("Video not loaded properly for conversion");
    const QString startEncoderError = tr("Failed to start encoder");
    const QString encodingFailedError = tr("Conversion failed: no video frames were encoded");
    const QString frameExtractionError = tr("Conversion failed while extracting video frames");
    const QString finalizeOutputError = tr("Conversion failed while saving the output file");
    const QString outputMissingError = tr("Conversion failed: output file is missing or empty");
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis,
                             videoPath,
                             sourceVideoPath,
                             requestedStartMs,
                             requestedEndMs,
                             cropRect,
                             outputPath,
                             selectedFormat,
                             createPlayerError,
                             loadVideoError,
                             invalidVideoError,
                             startEncoderError,
                             encodingFailedError,
                             frameExtractionError,
                             finalizeOutputError,
                             outputMissingError]() {
#ifdef Q_OS_WIN
        // Initialize COM for Media Foundation on this worker thread.
        // RAII ensures CoUninitialize is called on every return path.
        struct ComGuard {
            HRESULT hr;
            ComGuard() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
            ~ComGuard() { if (SUCCEEDED(hr)) CoUninitialize(); }
        } comGuard;
#endif

        auto postFailure = [weakThis](const QString& errorMessage) {
            QCoreApplication* app = QCoreApplication::instance();
            if (!app) {
                return;
            }

            QMetaObject::invokeMethod(app, [weakThis, errorMessage]() {
                if (!weakThis) {
                    return;
                }
                weakThis->setErrorMessage(errorMessage);
                weakThis->finishProcessing();
            }, Qt::QueuedConnection);
        };

        auto frameReader = IVideoFrameReader::create();
        std::unique_ptr<IVideoPlayer> player;
        if (frameReader) {
            if (!frameReader->load(videoPath)) {
                qWarning() << "RecordingPreviewBackend: Failed to open video reader:"
                           << frameReader->lastError();
                postFailure(loadVideoError);
                return;
            }
        } else {
            player.reset(IVideoPlayer::create(nullptr));
            if (!player) {
                qWarning() << "RecordingPreviewBackend: Failed to create video player for conversion";
                postFailure(createPlayerError);
                return;
            }

            bool mediaLoaded = false;
            {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);
                auto mediaLoadedConn = connect(player.get(), &IVideoPlayer::mediaLoaded, &loop, [&]() {
                    mediaLoaded = true;
                    loop.quit();
                });
                connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                if (!player->load(videoPath)) {
                    disconnect(mediaLoadedConn);
                    qWarning() << "RecordingPreviewBackend: Failed to load video for conversion";
                    postFailure(loadVideoError);
                    return;
                }

                if (!mediaLoaded) {
                    timer.start(kVideoLoadTimeoutMs);
                    loop.exec();
                    timer.stop();
                }
                disconnect(mediaLoadedConn);
            }
        }

        const QSize vidSize = frameReader ? frameReader->videoSize() : player->videoSize();
        int frameRateInt = static_cast<int>(frameReader ? frameReader->frameRate() : player->frameRate());
        if (frameRateInt <= 0) frameRateInt = 30;
        const qint64 dur = frameReader ? frameReader->duration() : player->duration();
        const qint64 startMs = qMax<qint64>(0, requestedStartMs);
        const qint64 endMs = requestedEndMs < 0 ? dur : qMin(requestedEndMs, dur);
        const QSize outputSize = cropRect.isEmpty() ? vidSize : cropRect.size();

        if (dur <= 0 || vidSize.isEmpty() || startMs >= endMs) {
            qWarning() << "RecordingPreviewBackend: Video not loaded properly for conversion";
            postFailure(invalidVideoError);
            return;
        }

        // Create encoder
        const QString workingOutputPath = outputPath + QStringLiteral(".part-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool encoderStarted = false;
        std::unique_ptr<NativeGifEncoder> gifEncoder;
        std::unique_ptr<WebPAnimationEncoder> webpEncoder;

        if (selectedFormat == GIF) {
            gifEncoder = std::make_unique<NativeGifEncoder>(nullptr);
            gifEncoder->setMaxBitDepth(16);
            encoderStarted = gifEncoder->start(workingOutputPath, outputSize, frameRateInt);
        } else {
            webpEncoder = std::make_unique<WebPAnimationEncoder>(nullptr);
            webpEncoder->setQuality(80);
            webpEncoder->setLooping(true);
            encoderStarted = webpEncoder->start(workingOutputPath, outputSize, frameRateInt);
        }

        if (!encoderStarted) {
            qWarning() << "RecordingPreviewBackend: Failed to start encoder";
            postFailure(startEncoderError);
            return;
        }

        bool encoderFinishedSuccessfully = false;
        QString encoderFinishedPath;
        if (gifEncoder) {
            connect(gifEncoder.get(), &NativeGifEncoder::finished,
                    [&encoderFinishedSuccessfully, &encoderFinishedPath](bool success,
                                                                          const QString &path) {
                encoderFinishedSuccessfully = success;
                encoderFinishedPath = path;
            });
        } else if (webpEncoder) {
            connect(webpEncoder.get(), &WebPAnimationEncoder::finished,
                    [&encoderFinishedSuccessfully, &encoderFinishedPath](bool success,
                                                                          const QString &path) {
                encoderFinishedSuccessfully = success;
                encoderFinishedPath = path;
            });
        }

        // Extract and encode frames
        if (player) {
            player->pause();
        }
        bool frameExtractionFailed = false;

        for (qint64 frameIndex = 0; ; ++frameIndex) {
            const qint64 timeMs = startMs + frameIndex * 1000 / frameRateInt;
            if (timeMs >= endMs) {
                break;
            }
            QImage capturedFrame;
            if (frameReader) {
                capturedFrame = frameReader->frameAt(timeMs);
                if (capturedFrame.isNull()) {
                    qWarning() << "RecordingPreviewBackend: Offline frame extraction failed:"
                               << frameReader->lastError();
                }
            } else {
                bool frameReceived = false;
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);

                auto conn = connect(player.get(), &IVideoPlayer::frameReady,
                                    &loop, [&capturedFrame, &frameReceived, &loop](const QImage &frame) {
                    capturedFrame = frame.copy();
                    frameReceived = true;
                    loop.quit();
                });
                connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                timer.start(kFrameExtractionTimeoutMs);
                player->seek(timeMs);
                if (!frameReceived) {
                    loop.exec();
                }
                timer.stop();
                disconnect(conn);
            }

            if (!capturedFrame.isNull()) {
                if (!cropRect.isEmpty()) {
                    const QRect frameRect(QPoint(0, 0), capturedFrame.size());
                    if (!frameRect.contains(cropRect)) {
                        qWarning() << "RecordingPreviewBackend: Crop rectangle" << cropRect
                                   << "exceeds frame bounds" << frameRect;
                        frameExtractionFailed = true;
                        break;
                    }
                    capturedFrame = capturedFrame.copy(cropRect);
                }
                const qint64 framesBefore = gifEncoder
                    ? gifEncoder->framesWritten()
                    : webpEncoder->framesWritten();
                if (gifEncoder)
                    gifEncoder->writeFrame(capturedFrame, timeMs - startMs);
                else if (webpEncoder)
                    webpEncoder->writeFrame(capturedFrame, timeMs - startMs);

                const qint64 framesAfter = gifEncoder
                    ? gifEncoder->framesWritten()
                    : webpEncoder->framesWritten();
                if (framesAfter != framesBefore + 1) {
                    frameExtractionFailed = true;
                    break;
                }
            } else {
                qWarning() << "RecordingPreviewBackend: Frame extraction failed at"
                           << timeMs << "ms";
                frameExtractionFailed = true;
                break;
            }

            int percent = static_cast<int>(((timeMs - startMs) * 100) / (endMs - startMs));
            QCoreApplication* app = QCoreApplication::instance();
            if (app) {
                QMetaObject::invokeMethod(app, [weakThis, percent]() {
                    if (!weakThis) {
                        return;
                    }
                    if (percent != weakThis->m_processProgress) {
                        weakThis->m_processProgress = percent;
                        emit weakThis->processProgressChanged();
                    }
                }, Qt::QueuedConnection);
            }
        }

        if (frameExtractionFailed) {
            if (gifEncoder) gifEncoder->abort();
            if (webpEncoder) webpEncoder->abort();
            QFile::remove(workingOutputPath);
            postFailure(frameExtractionError);
            return;
        }

        const qint64 encodedFrameCount = gifEncoder
            ? gifEncoder->framesWritten()
            : webpEncoder->framesWritten();

        // Finish encoding. Both encoders complete synchronously and report the
        // actual result through their finished signal.
        if (gifEncoder) gifEncoder->finish();
        if (webpEncoder) webpEncoder->finish();

        if (encodedFrameCount <= 0 || !encoderFinishedSuccessfully
            || encoderFinishedPath != workingOutputPath) {
            qWarning() << "RecordingPreviewBackend: Conversion encoder failed; keeping original";
            QFile::remove(workingOutputPath);
            postFailure(encodingFailedError);
            return;
        }

        if (QFileInfo::exists(outputPath)
            || !QFile::rename(workingOutputPath, outputPath)) {
            qWarning() << "RecordingPreviewBackend: Failed to promote conversion output";
            QFile::remove(workingOutputPath);
            postFailure(finalizeOutputError);
            return;
        }

        // Post results back to main thread
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            return;
        }

        const QSize croppedSize = cropRect.isEmpty() ? QSize() : outputSize;
        QMetaObject::invokeMethod(app, [weakThis, outputPath, sourceVideoPath, outputMissingError,
                                        croppedSize]() {
            if (!weakThis) {
                return;
            }

            weakThis->m_processProgress = 100;
            emit weakThis->processProgressChanged();
            weakThis->finishProcessing();

            // Clean up original MP4 only if output is valid
            QFileInfo outInfo(outputPath);
            if (outInfo.exists() && outInfo.size() > 0) {
                QFile::remove(sourceVideoPath);
                SnapTray::WindowTimelineSidecar::remove(sourceVideoPath);
                weakThis->m_saved = true;
                weakThis->close();
                emit weakThis->saveRequested(outputPath, croppedSize);
            } else {
                qWarning() << "RecordingPreviewBackend: Conversion output missing or empty, keeping original";
                weakThis->setErrorMessage(outputMissingError);
            }
        }, Qt::QueuedConnection);
    });
}

// ---------- MP4 trim / crop (runs on background thread) ----------

void RecordingPreviewBackend::performTranscode(bool smartSave)
{
    const int dotIndex = m_videoPath.lastIndexOf('.');
    const QString base = dotIndex > 0 ? m_videoPath.left(dotIndex) : m_videoPath;
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString outputPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".mp4");
    const QString workingPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".mp4");

    if (smartSave) {
        qDebug() << "RecordingPreviewBackend: Finalizing unedited recording";
    } else {
        qDebug() << "RecordingPreviewBackend: Exporting MP4 edits, trim:" << hasTrim()
                 << "crop:" << m_cropRect;
    }

    // Install the job and token before notifying QML or synchronous observers.
    m_exportCancelToken = std::make_shared<std::atomic_bool>(false);
    const auto cancelToken = m_exportCancelToken;
    m_exportKind = ExportKind::MP4;
    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Exporting video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    VideoTranscodeRequest request;
    request.inputPath = m_videoPath;
    request.outputPath = workingPath;
    request.startMs = m_trimStart;
    request.endMs = m_trimEnd;
    request.cropRect = m_cropRect;
    const QSize croppedSize = m_cropRect.isEmpty() ? QSize() : m_cropRect.size();
    const QString unsupportedError = tr("Video export is not supported on this platform");
    const QString failedTemplate = tr("Export failed: %1");
    // Transcoder and validation failures carry English diagnostics for the
    // log; the user sees this translated summary instead.
    const QString keptMessage = tr("Export failed; the original recording was kept.");
    const int outputQuality = m_outputQuality;
    const bool recordedAsIntermediate = m_recordedAsIntermediate;
    const TranscoderFactory createTranscoder = transcoderFactoryOverride()
        ? transcoderFactoryOverride()
        : TranscoderFactory(&IVideoTranscoder::create);
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis, request, outputPath, croppedSize, unsupportedError, failedTemplate,
                             keptMessage, cancelToken, createTranscoder, smartSave, outputQuality,
                             recordedAsIntermediate]() mutable {
        // Queued onto the GUI thread; never blocks, so it is safe from the
        // transcoder's worker threads (see IVideoTranscoder::ProgressCallback).
        auto post = [](auto fn) {
            if (QCoreApplication* app = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(app, fn, Qt::QueuedConnection);
                return true;
            }
            return false;
        };
        auto transcoder = createTranscoder();
        const bool unsupported = !transcoder;
        VideoTranscodeResult result;
        VideoFileProbe sourceProbe;
        bool moveInstead = false;
        const char* moveReason = "";
        if (unsupported) {
            // Without a transcoder an unedited save still works as before.
            moveInstead = smartSave;
            moveReason = "no transcoder on this platform";
            if (!moveInstead) result.errorMessage = unsupportedError;
        } else if (smartSave && !recordedAsIntermediate) {
            // Already encoded at the user's quality; a re-encode would only lose detail.
            moveInstead = true;
            moveReason = "not an intermediate: recorded at the selected quality; saving as recorded";
        } else {
            sourceProbe = transcoder->probe(request.inputPath);
            if (smartSave && !sourceProbe.valid) {
                // A re-encode would only fail validation and leave the user
                // with Discard; keep the recording as recorded.
                qWarning() << "RecordingPreviewBackend: unedited recording could not be probed; saving it as recorded";
                moveInstead = true;
                moveReason = "unprobeable";
            } else if (smartSave && SnapTray::IntermediateQuality::smartSaveShouldMove(
                                        sourceProbe, QFileInfo(request.inputPath).size(), outputQuality)) {
                moveInstead = true;
                moveReason = "meets the output quality target";
            }
        }
        if (moveInstead) {
            qDebug() << "RecordingPreviewBackend: moving the unedited recording;" << moveReason;
            const QString inputPath = request.inputPath;
            post([weakThis, inputPath, cancelToken]() {
                if (!weakThis) return;
                // GUI commit boundary: cancellation wins until this callback starts.
                const bool cancelled = cancelToken->load();
                weakThis->finishProcessing();
                if (cancelled) return;
                weakThis->m_saved = true;
                weakThis->close();
                emit weakThis->saveRequested(inputPath, QSize());
            });
            return;
        }
        if (!unsupported) {
            const IVideoTranscoder::ProgressCallback progressCallback =
                [weakThis, cancelToken, post](int percent) {
                    post([weakThis, percent, cancelToken]() {
                        if (weakThis && weakThis->m_isProcessing && weakThis->m_exportCancelToken == cancelToken
                            && !cancelToken->load() && weakThis->m_processProgress != percent) {
                            weakThis->m_processProgress = percent;
                            emit weakThis->processProgressChanged();
                        }
                    });
                    return !cancelToken->load();
                };
            const QSize outputSize = request.cropRect.isEmpty() ? sourceProbe.videoSize : request.cropRect.size();
            request.videoBitrate = SnapTray::IntermediateQuality::outputBitrateFor(
                outputSize, sourceProbe.frameRate, outputQuality);
            qDebug() << "RecordingPreviewBackend: exporting at" << request.videoBitrate << "bps for"
                     << outputSize << "quality" << outputQuality;
            result = transcoder->transcode(request, progressCallback);
        }
        // Do not trust the transcoder's claim alone: the source is deleted on
        // success, so the output must be a playable file that kept whatever
        // audio the selected range had. A range the source audio does not
        // reach (within kAudioCoverageToleranceMs) exports video only, as the
        // source is there; that is not lost audio.
        if (result.success) {
            const VideoFileProbe outputProbe = transcoder->probe(request.outputPath);
            const QFileInfo outputInfo(request.outputPath);
            const qint64 rangeStartMs = qMax<qint64>(0, request.startMs);
            const qint64 rangeEndMs = request.endMs < 0 ? sourceProbe.durationMs
                                                        : qMin(request.endMs, sourceProbe.durationMs);
            const qint64 audioCoverageMs = sourceProbe.hasAudio
                ? qMax<qint64>(0, qMin(sourceProbe.audioEndMs, rangeEndMs)
                                      - qMax(sourceProbe.audioStartMs, rangeStartMs))
                : 0;
            const bool expectAudio = audioCoverageMs > kAudioCoverageToleranceMs;
            // The picture must be exactly what was asked for: a crop the
            // transcoder had to shrink or ignore is a different export (and
            // would be named by the requested size).
            const QSize expectedSize = request.cropRect.isEmpty() ? sourceProbe.videoSize : request.cropRect.size();
            if (cancelToken->load() || !sourceProbe.valid || !outputProbe.valid
                || !outputInfo.exists() || outputInfo.size() <= 0 || outputProbe.durationMs <= 0
                || outputProbe.videoSize != expectedSize
                || (expectAudio && (!result.audioCopied || !outputProbe.hasAudio))
                || (result.audioCopied && !outputProbe.hasAudio)) {
                qWarning() << "RecordingPreviewBackend: MP4 export output failed validation;"
                           << "cancelled:" << cancelToken->load()
                           << "source valid:" << sourceProbe.valid
                           << "output valid:" << outputProbe.valid
                           << "output size:" << outputProbe.videoSize << "expected:" << expectedSize
                           << "source audio:" << sourceProbe.hasAudio
                           << "audio coverage (ms):" << audioCoverageMs
                           << "audio copied:" << result.audioCopied
                           << "output audio:" << outputProbe.hasAudio;
                QFile::remove(request.outputPath);
                result.success = false;
                result.errorMessage = QStringLiteral("Output validation failed; source retained");
            }
        } else {
            qWarning() << "RecordingPreviewBackend: MP4 export failed:" << result.errorMessage;
            // The transcoder removes its own output on failure; a fake or
            // misbehaving one must not leave a partial file behind either.
            QFile::remove(request.outputPath);
        }
        if (result.success && !QFile::rename(request.outputPath, outputPath)) {
            qWarning() << "RecordingPreviewBackend: Failed to promote MP4 export output";
            QFile::remove(request.outputPath);
            result.success = false;
            result.errorMessage = QStringLiteral("rename failed");
        }
        // A cancelled export keeps the source and shows no error. Cancellation
        // may still arrive after promotion, before the GUI commits the result.
        const bool posted = post([weakThis, result, outputPath, croppedSize, failedTemplate, keptMessage,
                                  unsupported, unsupportedError, inputPath = request.inputPath, cancelToken,
                                  smartSave]() {
            if (!weakThis) {
                // The preview went away mid-export; keep the source, drop the output.
                QFile::remove(outputPath);
                return;
            }
            // Read on the GUI thread, not before posting: the user may have
            // cancelled while this completion was waiting in the event queue.
            const bool cancelled = cancelToken->load();
            if (cancelled) QFile::remove(outputPath);
            weakThis->finishProcessing();
            if (cancelled) {
                qDebug() << "RecordingPreviewBackend: export cancelled; the original recording was kept";
                return;
            }
            if (!result.success) {
                if (smartSave) {
                    // An unedited recording is still a valid recording: save
                    // it as recorded rather than leave the user with Discard.
                    qWarning() << "RecordingPreviewBackend: moving the unedited recording; re-encode failed:"
                               << result.errorMessage;
                    weakThis->m_saved = true;
                    weakThis->close();
                    emit weakThis->saveRequested(inputPath, QSize());
                    return;
                }
                weakThis->setErrorMessage(unsupported ? failedTemplate.arg(unsupportedError) : keptMessage);
                return;
            }
            QFile::remove(inputPath);
            SnapTray::WindowTimelineSidecar::remove(inputPath);
            weakThis->m_saved = true;
            weakThis->close();
            emit weakThis->saveRequested(outputPath, croppedSize);
        });
        if (!posted && result.success) {
            QFile::remove(outputPath);
        }
    });
}
