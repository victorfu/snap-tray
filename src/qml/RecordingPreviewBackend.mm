#include "qml/RecordingPreviewBackend.h"
#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"
#include "video/VideoTrimmer.h"
#include "video/IVideoPlayer.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"

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

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

#ifdef Q_OS_WIN
#include <objbase.h>
#endif

RecordingPreviewBackend::RecordingPreviewBackend(const QString &videoPath, QObject *parent)
    : QObject(parent)
    , m_videoPath(videoPath)
{
}

RecordingPreviewBackend::~RecordingPreviewBackend()
{
    if (m_trimmer) {
        m_trimmer->cancel();
        delete m_trimmer;
        m_trimmer = nullptr;
    }

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
    m_view->setFlag(Qt::WindowStaysOnTopHint, true);
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
        emit closed(m_saved);
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
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
    }
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
    if (m_isProcessing) {
        qDebug() << "RecordingPreviewBackend: Save ignored while processing";
        return;
    }

    // If trimming is needed, do that first
    if (hasTrim()) {
        performTrim();
        return;
    }

    if (m_selectedFormat == MP4) {
        // MP4 -- use original file directly
        m_saved = true;
        const QString outputPath = m_videoPath;
        close(); // Release playback resources before caller touches the file.
        emit saveRequested(outputPath);
    } else {
        // Convert to GIF or WebP on a background thread
        performFormatConversion(m_selectedFormat);
    }
}

void RecordingPreviewBackend::discard()
{
    qDebug() << "RecordingPreviewBackend: Discard requested";
    if (m_isProcessing) {
        qDebug() << "RecordingPreviewBackend: Discard ignored while processing";
        return;
    }
    m_saved = false;
    const QString discardPath = m_videoPath;
    close(); // Release playback resources before caller deletes the file.
    emit discardRequested(discardPath);
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

    qDebug() << "RecordingPreviewBackend: Converting to" << extension;

    // Show processing state
    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Converting video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    // Capture values needed by the worker thread
    const QString videoPath = m_videoPath;
    const QString sourceVideoPath = m_videoPath;
    const int selectedFormat = static_cast<int>(format);
    const QString createPlayerError = tr("Failed to create video player for conversion");
    const QString loadVideoError = tr("Failed to load video for conversion");
    const QString invalidVideoError = tr("Video not loaded properly for conversion");
    const QString startEncoderError = tr("Failed to start encoder");
    const QString outputMissingError = tr("Conversion failed: output file is missing or empty");
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis,
                             videoPath,
                             sourceVideoPath,
                             outputPath,
                             selectedFormat,
                             createPlayerError,
                             loadVideoError,
                             invalidVideoError,
                             startEncoderError,
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
                weakThis->m_isProcessing = false;
                emit weakThis->processingChanged();
            }, Qt::QueuedConnection);
        };

        // Create a temporary IVideoPlayer for frame extraction
        std::unique_ptr<IVideoPlayer> player(IVideoPlayer::create(nullptr));
        if (!player) {
            qWarning() << "RecordingPreviewBackend: Failed to create video player for conversion";
            postFailure(createPlayerError);
            return;
        }

        // Load video synchronously on worker thread
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
                timer.start(5000);
                loop.exec();
                timer.stop();
            }
            disconnect(mediaLoadedConn);
        }

        QSize vidSize = player->videoSize();
        int frameRateInt = static_cast<int>(player->frameRate());
        if (frameRateInt <= 0) frameRateInt = 30;
        qint64 dur = player->duration();

        if (dur <= 0 || vidSize.isEmpty()) {
            qWarning() << "RecordingPreviewBackend: Video not loaded properly for conversion";
            postFailure(invalidVideoError);
            return;
        }

        // Create encoder
        bool encoderStarted = false;
        std::unique_ptr<NativeGifEncoder> gifEncoder;
        std::unique_ptr<WebPAnimationEncoder> webpEncoder;

        if (selectedFormat == GIF) {
            gifEncoder = std::make_unique<NativeGifEncoder>(nullptr);
            gifEncoder->setMaxBitDepth(16);
            encoderStarted = gifEncoder->start(outputPath, vidSize, frameRateInt);
        } else {
            webpEncoder = std::make_unique<WebPAnimationEncoder>(nullptr);
            webpEncoder->setQuality(80);
            webpEncoder->setLooping(true);
            encoderStarted = webpEncoder->start(outputPath, vidSize, frameRateInt);
        }

        if (!encoderStarted) {
            qWarning() << "RecordingPreviewBackend: Failed to start encoder";
            postFailure(startEncoderError);
            return;
        }

        // Extract and encode frames
        qint64 frameInterval = 1000 / frameRateInt;
        player->pause();

        for (qint64 timeMs = 0; timeMs <= dur; timeMs += frameInterval) {
            QImage capturedFrame;
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

            player->seek(timeMs);
            timer.start(500);
            loop.exec();

            timer.stop();
            disconnect(conn);

            if (frameReceived && !capturedFrame.isNull()) {
                if (gifEncoder)
                    gifEncoder->writeFrame(capturedFrame, timeMs);
                else if (webpEncoder)
                    webpEncoder->writeFrame(capturedFrame, timeMs);
            } else {
                qDebug() << "RecordingPreviewBackend: Skipped frame at" << timeMs << "ms (no frame received)";
            }

            int percent = static_cast<int>((timeMs * 100) / dur);
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

        // Finish encoding
        if (gifEncoder) gifEncoder->finish();
        if (webpEncoder) webpEncoder->finish();

        // Post results back to main thread
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            return;
        }

        QMetaObject::invokeMethod(app, [weakThis, outputPath, sourceVideoPath, outputMissingError]() {
            if (!weakThis) {
                return;
            }

            weakThis->m_processProgress = 100;
            emit weakThis->processProgressChanged();
            weakThis->m_isProcessing = false;
            emit weakThis->processingChanged();

            // Clean up original MP4 only if output is valid
            QFileInfo outInfo(outputPath);
            if (outInfo.exists() && outInfo.size() > 0) {
                QFile::remove(sourceVideoPath);
                weakThis->m_saved = true;
                weakThis->close();
                emit weakThis->saveRequested(outputPath);
            } else {
                qWarning() << "RecordingPreviewBackend: Conversion output missing or empty, keeping original";
                weakThis->setErrorMessage(outputMissingError);
            }
        }, Qt::QueuedConnection);
    });
}

// ---------- Trim ----------

void RecordingPreviewBackend::performTrim()
{
    // Determine output format
    QString extension;
    EncoderFactory::Format encoderFormat;
    switch (m_selectedFormat) {
    case GIF:
        extension = ".gif";
        encoderFormat = EncoderFactory::Format::GIF;
        break;
    case WebP:
        extension = ".webp";
        encoderFormat = EncoderFactory::Format::WebP;
        break;
    default:
        extension = ".mp4";
        encoderFormat = EncoderFactory::Format::MP4;
        break;
    }

    // Create output path with timestamp for uniqueness
    QString outputPath = m_videoPath;
    int dotIndex = outputPath.lastIndexOf('.');
    QString timestamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    if (dotIndex > 0)
        outputPath = outputPath.left(dotIndex) + "_trimmed_" + timestamp + extension;
    else
        outputPath += "_trimmed_" + timestamp + extension;

    // Show processing state
    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Trimming video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    // Create trimmer
    m_trimmer = new VideoTrimmer(this);
    m_trimmer->setInputPath(m_videoPath);
    m_trimmer->setTrimRange(m_trimStart, trimEnd());
    m_trimmer->setOutputFormat(encoderFormat);
    m_trimmer->setOutputPath(outputPath);

    connect(m_trimmer, &VideoTrimmer::progress,
            this, &RecordingPreviewBackend::onTrimProgress);
    connect(m_trimmer, &VideoTrimmer::finished,
            this, &RecordingPreviewBackend::onTrimFinished);
    connect(m_trimmer, &VideoTrimmer::error,
            this, [this](const QString &msg) {
        qWarning() << "RecordingPreviewBackend: Trim error:" << msg;
        setErrorMessage(tr("Trim failed: %1").arg(msg));
        m_isProcessing = false;
        emit processingChanged();
    });

    m_trimmer->startTrim();
}

void RecordingPreviewBackend::onTrimProgress(int percent)
{
    if (m_processProgress != percent) {
        m_processProgress = percent;
        emit processProgressChanged();
    }
}

void RecordingPreviewBackend::onTrimFinished(bool success, const QString &outputPath)
{
    qDebug() << "RecordingPreviewBackend: Trim finished, success:" << success;

    // Clean up trimmer
    if (m_trimmer) {
        m_trimmer->deleteLater();
        m_trimmer = nullptr;
    }

    m_isProcessing = false;
    emit processingChanged();

    if (success) {
        QFileInfo outInfo(outputPath);
        if (outInfo.exists() && outInfo.size() > 0) {
            QFile::remove(m_videoPath);
        } else {
            qWarning() << "RecordingPreviewBackend: Trim output missing or empty, keeping original";
            setErrorMessage(tr("Trim failed: output file is missing or empty"));
            return;
        }
        m_saved = true;
        close();
        emit saveRequested(outputPath);
    } else {
        setErrorMessage(tr("Trim failed"));
    }
}
