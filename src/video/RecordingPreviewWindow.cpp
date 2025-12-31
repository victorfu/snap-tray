#include "video/RecordingPreviewWindow.h"
#include "video/VideoPlaybackWidget.h"
#include "video/TrimTimeline.h"
#include "video/VideoTrimmer.h"
#include "video/FormatSelectionWidget.h"
#include "encoding/EncoderFactory.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"
#include "ToolbarStyle.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QProgressDialog>
#include <QTimer>
#include <QComboBox>
#include <QDebug>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

constexpr float RecordingPreviewWindow::kSpeedOptions[];

RecordingPreviewWindow::RecordingPreviewWindow(const QString &videoPath,
                                               QWidget *parent)
    : QWidget(parent)
    , m_videoPath(videoPath)
    , m_saved(false)
    , m_wasPlayingBeforeScrub(false)
    , m_trimPreviewEnabled(false)
    , m_duration(0)
    , m_trimmer(nullptr)
    , m_trimProgressDialog(nullptr)
{
    // Window setup
    setWindowTitle("Recording Preview");
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);

    // Size constraints
    setMinimumSize(640, 480);
    resize(1024, 768);

    // Center on screen
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        move(screenGeometry.center() - rect().center());
    }

    setupUI();

    // Load video
    if (m_videoWidget->loadVideo(videoPath)) {
        qDebug() << "RecordingPreviewWindow: Loading video:" << videoPath;
    } else {
        qWarning() << "RecordingPreviewWindow: Failed to load video:" << videoPath;
    }
}

RecordingPreviewWindow::~RecordingPreviewWindow()
{
    qDebug() << "RecordingPreviewWindow: Destroyed";
}

void RecordingPreviewWindow::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Video widget
    m_videoWidget = new VideoPlaybackWidget(this);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_videoWidget);

    // Connect video signals
    connect(m_videoWidget, &VideoPlaybackWidget::positionChanged,
            this, &RecordingPreviewWindow::onPositionChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::durationChanged,
            this, &RecordingPreviewWindow::onDurationChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::stateChanged,
            this, &RecordingPreviewWindow::onStateChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::videoLoaded,
            this, &RecordingPreviewWindow::onVideoLoaded);
    connect(m_videoWidget, &VideoPlaybackWidget::errorOccurred,
            this, &RecordingPreviewWindow::onVideoError);

    // Timeline with trim handles
    m_timeline = new TrimTimeline(this);
    mainLayout->addWidget(m_timeline);

    // Connect timeline signals
    connect(m_timeline, &TrimTimeline::seekRequested,
            this, &RecordingPreviewWindow::onTimelineSeek);
    connect(m_timeline, &TrimTimeline::scrubbingStarted,
            this, &RecordingPreviewWindow::onScrubbingStarted);
    connect(m_timeline, &TrimTimeline::scrubbingEnded,
            this, &RecordingPreviewWindow::onScrubbingEnded);

    // Connect trim signals
    connect(m_timeline, &TrimTimeline::trimRangeChanged,
            this, &RecordingPreviewWindow::onTrimRangeChanged);
    connect(m_timeline, &TrimTimeline::trimHandleDoubleClicked,
            this, &RecordingPreviewWindow::onTrimHandleDoubleClicked);

    // Controls container
    QWidget *controlsContainer = new QWidget(this);
    controlsContainer->setFixedHeight(48);
    QHBoxLayout *controlsLayout = new QHBoxLayout(controlsContainer);
    controlsLayout->setContentsMargins(10, 5, 10, 5);
    controlsLayout->setSpacing(10);

    // Play/Pause button
    m_playPauseBtn = new QPushButton(this);
    m_playPauseBtn->setFixedSize(32, 32);
    m_playPauseBtn->setFlat(true);
    m_playPauseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_playPauseBtn, &QPushButton::clicked,
            this, &RecordingPreviewWindow::onPlayPauseClicked);
    controlsLayout->addWidget(m_playPauseBtn);

    // Time label
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setFixedWidth(100);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    controlsLayout->addWidget(m_timeLabel);

    // Speed combo box
    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItem("0.25x", 0.25f);
    m_speedCombo->addItem("0.5x", 0.5f);
    m_speedCombo->addItem("0.75x", 0.75f);
    m_speedCombo->addItem("1.0x", 1.0f);
    m_speedCombo->addItem("1.25x", 1.25f);
    m_speedCombo->addItem("1.5x", 1.5f);
    m_speedCombo->addItem("2.0x", 2.0f);
    m_speedCombo->setCurrentIndex(kDefaultSpeedIndex);
    m_speedCombo->setFixedWidth(70);
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingPreviewWindow::onSpeedChanged);
    controlsLayout->addWidget(m_speedCombo);

    controlsLayout->addStretch();

    // Volume controls
    m_muteBtn = new QPushButton(this);
    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setFlat(true);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_muteBtn, &QPushButton::clicked,
            this, &RecordingPreviewWindow::onMuteToggled);
    controlsLayout->addWidget(m_muteBtn);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setMinimum(0);
    m_volumeSlider->setMaximum(100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(80);
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &RecordingPreviewWindow::onVolumeChanged);
    controlsLayout->addWidget(m_volumeSlider);

    controlsLayout->addSpacing(10);

    // Trim preview checkbox
    m_trimPreviewCheckbox = new QCheckBox(tr("Preview trimmed only"), this);
    m_trimPreviewCheckbox->setChecked(false);
    m_trimPreviewEnabled = false;
    connect(m_trimPreviewCheckbox, &QCheckBox::toggled,
            this, &RecordingPreviewWindow::onTrimPreviewToggled);
    controlsLayout->addWidget(m_trimPreviewCheckbox);

    controlsLayout->addSpacing(10);

    // Format selection widget
    m_formatWidget = new FormatSelectionWidget(this);
    controlsLayout->addWidget(m_formatWidget);

    controlsLayout->addSpacing(10);

    // Action buttons
    m_discardBtn = new QPushButton("Discard", this);
    m_discardBtn->setFixedHeight(32);
    m_discardBtn->setCursor(Qt::PointingHandCursor);
    connect(m_discardBtn, &QPushButton::clicked,
            this, &RecordingPreviewWindow::onDiscardClicked);
    controlsLayout->addWidget(m_discardBtn);

    m_saveBtn = new QPushButton("Save", this);
    m_saveBtn->setFixedHeight(32);
    m_saveBtn->setCursor(Qt::PointingHandCursor);
    m_saveBtn->setDefault(true);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &RecordingPreviewWindow::onSaveClicked);
    controlsLayout->addWidget(m_saveBtn);

    mainLayout->addWidget(controlsContainer);

    // Initial button states
    updatePlayPauseButton();
    updateTimeLabel();

    // Update mute button icon
    m_muteBtn->setText(m_videoWidget->isMuted() ? "Muted" : "Vol");
}

void RecordingPreviewWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "RecordingPreviewWindow: closeEvent, saved:" << m_saved;
    emit closed(m_saved);
    event->accept();
}

void RecordingPreviewWindow::keyPressEvent(QKeyEvent *event)
{
    // Check for Ctrl/Cmd+S
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_S) {
            onSaveClicked();
            return;
        }
    }

    switch (event->key()) {
    case Qt::Key_Space:
        onPlayPauseClicked();
        break;
    case Qt::Key_Escape:
        onDiscardClicked();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        onSaveClicked();
        break;
    case Qt::Key_M:
        onMuteToggled();
        break;
    case Qt::Key_Left:
        seekRelative(-5000);  // -5 seconds
        break;
    case Qt::Key_Right:
        seekRelative(5000);   // +5 seconds
        break;
    case Qt::Key_Comma:
        stepBackward();       // Previous frame (~33ms)
        break;
    case Qt::Key_Period:
        stepForward();        // Next frame (~33ms)
        break;
    case Qt::Key_BracketLeft:
        adjustSpeed(-1);      // Decrease speed
        break;
    case Qt::Key_BracketRight:
        adjustSpeed(1);       // Increase speed
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void RecordingPreviewWindow::onPlayPauseClicked()
{
    m_videoWidget->togglePlayPause();
    updatePlayPauseButton();
}

void RecordingPreviewWindow::onPositionChanged(qint64 positionMs)
{
    if (!m_timeline->isScrubbing()) {
        m_timeline->setPosition(positionMs);
    }
    updateTimeLabel();
}

void RecordingPreviewWindow::onDurationChanged(qint64 durationMs)
{
    m_duration = durationMs;
    m_timeline->setDuration(durationMs);
    updateTimeLabel();
}

void RecordingPreviewWindow::onVideoLoaded()
{
    qDebug() << "RecordingPreviewWindow: Video loaded, duration:" << m_videoWidget->duration();

    // Check if this is a GIF (by extension) and enable looping
    if (m_videoPath.endsWith(".gif", Qt::CaseInsensitive)) {
        m_videoWidget->setLooping(true);
        qDebug() << "RecordingPreviewWindow: GIF detected, looping enabled";
    }

    // Auto-play on load
    m_videoWidget->play();
    updatePlayPauseButton();
}

void RecordingPreviewWindow::onVideoError(const QString &message)
{
    qWarning() << "RecordingPreviewWindow: Video error:" << message;
    QMessageBox::warning(this, "Video Error",
                         QString("Failed to load video:\n%1").arg(message));
}

void RecordingPreviewWindow::onStateChanged(IVideoPlayer::State state)
{
    Q_UNUSED(state)
    updatePlayPauseButton();
}

void RecordingPreviewWindow::onSaveClicked()
{
    qDebug() << "RecordingPreviewWindow: Save clicked";
    m_videoWidget->stop();

    auto format = m_formatWidget->selectedFormat();

    // Check if we need to trim
    if (m_timeline->hasTrim()) {
        qDebug() << "RecordingPreviewWindow: Trimming video from"
                 << m_timeline->trimStart() << "to" << m_timeline->trimEnd();
        performTrim();
        return;  // Will close after trim completes
    }

    QString outputPath;

    if (format == FormatSelectionWidget::Format::MP4) {
        // Use original MP4 file directly
        outputPath = m_videoPath;
    } else {
        // Convert to selected format
        outputPath = convertToFormat(format);
        if (outputPath.isEmpty()) {
            // Conversion failed, don't close
            return;
        }
    }

    m_saved = true;
    emit saveRequested(outputPath);
    close();
}

void RecordingPreviewWindow::onDiscardClicked()
{
    qDebug() << "RecordingPreviewWindow: Discard clicked";
    m_saved = false;
    m_videoWidget->stop();
    emit discardRequested();
    close();
}

void RecordingPreviewWindow::onVolumeChanged(int value)
{
    float volume = value / 100.0f;
    m_videoWidget->setVolume(volume);

    // Update mute button if volume becomes 0
    if (value == 0 && !m_videoWidget->isMuted()) {
        m_videoWidget->setMuted(true);
    } else if (value > 0 && m_videoWidget->isMuted()) {
        m_videoWidget->setMuted(false);
    }

    m_muteBtn->setText(value == 0 ? "Muted" : "Vol");
}

void RecordingPreviewWindow::onMuteToggled()
{
    bool muted = !m_videoWidget->isMuted();
    m_videoWidget->setMuted(muted);
    m_muteBtn->setText(muted ? "Muted" : "Vol");

    // Update slider position
    if (muted) {
        m_volumeSlider->setValue(0);
    } else {
        m_volumeSlider->setValue((int)(m_videoWidget->volume() * 100));
    }
}

void RecordingPreviewWindow::onTimelineSeek(qint64 positionMs)
{
    m_videoWidget->seek(positionMs);
    updateTimeLabel();
}

void RecordingPreviewWindow::onScrubbingStarted()
{
    m_wasPlayingBeforeScrub = (m_videoWidget->state() == IVideoPlayer::State::Playing);
    if (m_wasPlayingBeforeScrub) {
        m_videoWidget->pause();
    }
}

void RecordingPreviewWindow::onScrubbingEnded()
{
    if (m_wasPlayingBeforeScrub) {
        m_videoWidget->play();
    }
}

void RecordingPreviewWindow::onSpeedChanged(int index)
{
    if (index >= 0 && index < (int)(sizeof(kSpeedOptions) / sizeof(kSpeedOptions[0]))) {
        float rate = kSpeedOptions[index];
        m_videoWidget->setPlaybackRate(rate);
        qDebug() << "RecordingPreviewWindow: Speed changed to" << rate << "x";
    }
}

void RecordingPreviewWindow::stepForward()
{
    // Step forward by ~33ms (one frame at 30fps)
    qint64 newPos = qMin(m_videoWidget->position() + 33, m_duration);
    m_videoWidget->seek(newPos);
}

void RecordingPreviewWindow::stepBackward()
{
    // Step backward by ~33ms (one frame at 30fps)
    qint64 newPos = qMax(m_videoWidget->position() - 33, qint64(0));
    m_videoWidget->seek(newPos);
}

void RecordingPreviewWindow::adjustSpeed(int delta)
{
    int newIndex = m_speedCombo->currentIndex() + delta;
    newIndex = qBound(0, newIndex, m_speedCombo->count() - 1);
    m_speedCombo->setCurrentIndex(newIndex);
}

void RecordingPreviewWindow::seekRelative(qint64 deltaMs)
{
    qint64 newPos = qBound(qint64(0), m_videoWidget->position() + deltaMs, m_duration);
    m_videoWidget->seek(newPos);
}

void RecordingPreviewWindow::updateTimeLabel()
{
    qint64 pos = m_videoWidget->position();
    qint64 dur = m_videoWidget->duration();
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(pos), formatTime(dur)));
}

void RecordingPreviewWindow::updatePlayPauseButton()
{
    bool isPlaying = m_videoWidget->state() == IVideoPlayer::State::Playing;
    m_playPauseBtn->setText(isPlaying ? "||" : ">");
}

QString RecordingPreviewWindow::formatTime(qint64 ms) const
{
    int totalSeconds = (int)(ms / 1000);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString RecordingPreviewWindow::convertToFormat(FormatSelectionWidget::Format format)
{
    // Determine output path and extension
    QString extension;
    switch (format) {
    case FormatSelectionWidget::Format::GIF:
        extension = ".gif";
        break;
    case FormatSelectionWidget::Format::WebP:
        extension = ".webp";
        break;
    default:
        return m_videoPath;  // MP4, no conversion needed
    }

    // Create output path by replacing extension
    QString outputPath = m_videoPath;
    int dotIndex = outputPath.lastIndexOf('.');
    if (dotIndex > 0) {
        outputPath = outputPath.left(dotIndex) + extension;
    } else {
        outputPath += extension;
    }

    qDebug() << "RecordingPreviewWindow: Converting to" << extension << ":" << outputPath;

    // Get video properties
    QSize videoSize = m_videoWidget->videoSize();
    int frameRate = 30;  // Assume 30fps
    qint64 duration = m_videoWidget->duration();

    if (duration <= 0 || videoSize.isEmpty()) {
        QMessageBox::warning(this, tr("Conversion Error"),
                            tr("Cannot convert: video not loaded properly."));
        return QString();
    }

    // Create progress dialog
    QProgressDialog progress(tr("Converting video..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    // Create encoder
    bool encoderStarted = false;
    NativeGifEncoder *gifEncoder = nullptr;
    WebPAnimationEncoder *webpEncoder = nullptr;

    if (format == FormatSelectionWidget::Format::GIF) {
        gifEncoder = new NativeGifEncoder(this);
        gifEncoder->setMaxBitDepth(16);
        encoderStarted = gifEncoder->start(outputPath, videoSize, frameRate);
    } else {
        webpEncoder = new WebPAnimationEncoder(this);
        webpEncoder->setQuality(80);
        webpEncoder->setLooping(true);
        encoderStarted = webpEncoder->start(outputPath, videoSize, frameRate);
    }

    if (!encoderStarted) {
        QString error = gifEncoder ? gifEncoder->lastError() : webpEncoder->lastError();
        QMessageBox::warning(this, tr("Conversion Error"),
                            tr("Failed to start encoder: %1").arg(error));
        delete gifEncoder;
        delete webpEncoder;
        return QString();
    }

    // Calculate frame interval (33ms for 30fps)
    qint64 frameInterval = 1000 / frameRate;
    qint64 totalFrames = duration / frameInterval;
    qint64 framesWritten = 0;
    bool cancelled = false;

    // Ensure video is paused for frame extraction
    m_videoWidget->pause();

    // Frame capture variables
    QImage capturedFrame;
    bool frameReceived = false;

    // Extract and encode frames
    for (qint64 timeMs = 0; timeMs <= duration && !cancelled; timeMs += frameInterval) {
        // Reset for this frame
        frameReceived = false;
        capturedFrame = QImage();

        // Create event loop and timer for this iteration
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        // Single connection that both captures frame and quits loop
        auto frameConnection = connect(m_videoWidget, &VideoPlaybackWidget::frameReady,
                                       this, [&capturedFrame, &frameReceived, &loop](const QImage &frame) {
            capturedFrame = frame.copy();
            frameReceived = true;
            loop.quit();
        });
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        // Seek to frame position
        m_videoWidget->seek(timeMs);

        // Wait for frame (with timeout)
        timer.start(500);  // 500ms timeout per frame
        loop.exec();

        // Stop timer if still running and disconnect
        timer.stop();
        disconnect(frameConnection);

        // Write frame if received
        if (frameReceived && !capturedFrame.isNull()) {
            if (gifEncoder) {
                gifEncoder->writeFrame(capturedFrame, timeMs);
            } else if (webpEncoder) {
                webpEncoder->writeFrame(capturedFrame, timeMs);
            }
            framesWritten++;
        }

        // Update progress
        int progressPercent = static_cast<int>((timeMs * 100) / duration);
        progress.setValue(progressPercent);

        // Check for cancellation
        QCoreApplication::processEvents();
        if (progress.wasCanceled()) {
            cancelled = true;
        }
    }

    // Finish or abort encoding
    if (cancelled) {
        if (gifEncoder) gifEncoder->abort();
        if (webpEncoder) webpEncoder->abort();
        delete gifEncoder;
        delete webpEncoder;
        return QString();
    }

    // Finish encoding
    if (gifEncoder) {
        gifEncoder->finish();
    } else if (webpEncoder) {
        webpEncoder->finish();
    }

    progress.setValue(100);

    qDebug() << "RecordingPreviewWindow: Conversion complete -" << framesWritten << "frames";

    // Clean up original MP4 file
    QFile::remove(m_videoPath);

    delete gifEncoder;
    delete webpEncoder;

    return outputPath;
}

// ============================================================================
// Trim slot implementations
// ============================================================================

void RecordingPreviewWindow::onTrimRangeChanged(qint64 startMs, qint64 endMs)
{
    qDebug() << "RecordingPreviewWindow: Trim range changed:" << startMs << "-" << endMs;

    // Update time label to show trimmed duration if trim is active
    updateTimeLabel();

    // If trim preview is enabled, constrain playback
    if (m_trimPreviewEnabled && m_videoWidget->state() == IVideoPlayer::State::Playing) {
        qint64 pos = m_videoWidget->position();
        if (pos < startMs) {
            m_videoWidget->seek(startMs);
        } else if (pos >= endMs) {
            m_videoWidget->seek(startMs);
        }
    }
}

void RecordingPreviewWindow::onTrimHandleDoubleClicked(bool isStartHandle)
{
    showTrimTimeInputDialog(isStartHandle);
}

void RecordingPreviewWindow::onTrimPreviewToggled(bool enabled)
{
    m_trimPreviewEnabled = enabled;
    qDebug() << "RecordingPreviewWindow: Trim preview" << (enabled ? "enabled" : "disabled");

    if (enabled && m_timeline->hasTrim()) {
        // Jump to start of trim range
        m_videoWidget->seek(m_timeline->trimStart());
    }
}

void RecordingPreviewWindow::onTrimProgress(int percent)
{
    if (m_trimProgressDialog) {
        m_trimProgressDialog->setValue(percent);
    }
}

void RecordingPreviewWindow::onTrimFinished(bool success, const QString &outputPath)
{
    qDebug() << "RecordingPreviewWindow: Trim finished, success:" << success;

    // Clean up progress dialog
    if (m_trimProgressDialog) {
        m_trimProgressDialog->close();
        delete m_trimProgressDialog;
        m_trimProgressDialog = nullptr;
    }

    // Clean up trimmer
    if (m_trimmer) {
        delete m_trimmer;
        m_trimmer = nullptr;
    }

    if (success) {
        // Delete original file
        QFile::remove(m_videoPath);

        m_saved = true;
        emit saveRequested(outputPath);
        close();
    } else {
        QMessageBox::warning(this, tr("Trim Failed"),
                            tr("Failed to trim the video. Please try again."));
    }
}

void RecordingPreviewWindow::showTrimTimeInputDialog(bool isStartHandle)
{
    QString title = isStartHandle ? tr("Set Trim Start") : tr("Set Trim End");
    qint64 currentValue = isStartHandle ? m_timeline->trimStart() : m_timeline->trimEnd();

    // Convert to seconds with one decimal
    double seconds = currentValue / 1000.0;

    bool ok;
    double newSeconds = QInputDialog::getDouble(
        this,
        title,
        tr("Enter time in seconds:"),
        seconds,
        0.0,
        m_duration / 1000.0,
        1,  // decimals
        &ok
    );

    if (ok) {
        qint64 newMs = static_cast<qint64>(newSeconds * 1000);
        if (isStartHandle) {
            // Ensure start < end
            qint64 end = m_timeline->trimEnd();
            if (end < 0) end = m_duration;
            if (newMs < end) {
                m_timeline->setTrimRange(newMs, end);
            }
        } else {
            // Ensure end > start
            qint64 start = m_timeline->trimStart();
            if (newMs > start) {
                m_timeline->setTrimRange(start, newMs);
            }
        }
    }
}

void RecordingPreviewWindow::performTrim()
{
    auto format = m_formatWidget->selectedFormat();

    // Determine output path
    QString extension;
    EncoderFactory::Format encoderFormat;
    switch (format) {
    case FormatSelectionWidget::Format::GIF:
        extension = ".gif";
        encoderFormat = EncoderFactory::Format::GIF;
        break;
    case FormatSelectionWidget::Format::WebP:
        extension = ".webp";
        encoderFormat = EncoderFactory::Format::WebP;
        break;
    case FormatSelectionWidget::Format::MP4:
    default:
        extension = ".mp4";
        encoderFormat = EncoderFactory::Format::MP4;
        break;
    }

    // Create output path
    QString outputPath = m_videoPath;
    int dotIndex = outputPath.lastIndexOf('.');
    if (dotIndex > 0) {
        outputPath = outputPath.left(dotIndex) + "_trimmed" + extension;
    } else {
        outputPath += "_trimmed" + extension;
    }

    // Create trimmer
    m_trimmer = new VideoTrimmer(this);
    m_trimmer->setInputPath(m_videoPath);
    m_trimmer->setTrimRange(m_timeline->trimStart(), m_timeline->trimEnd());
    m_trimmer->setOutputFormat(encoderFormat);
    m_trimmer->setOutputPath(outputPath);

    connect(m_trimmer, &VideoTrimmer::progress,
            this, &RecordingPreviewWindow::onTrimProgress);
    connect(m_trimmer, &VideoTrimmer::finished,
            this, &RecordingPreviewWindow::onTrimFinished);
    connect(m_trimmer, &VideoTrimmer::error,
            this, [this](const QString &msg) {
        qWarning() << "RecordingPreviewWindow: Trim error:" << msg;
        if (m_trimProgressDialog) {
            m_trimProgressDialog->close();
            delete m_trimProgressDialog;
            m_trimProgressDialog = nullptr;
        }
        QMessageBox::warning(this, tr("Trim Error"), msg);
    });

    // Create progress dialog
    m_trimProgressDialog = new QProgressDialog(tr("Trimming video..."), tr("Cancel"), 0, 100, this);
    m_trimProgressDialog->setWindowModality(Qt::WindowModal);
    m_trimProgressDialog->setMinimumDuration(0);
    m_trimProgressDialog->setValue(0);

    connect(m_trimProgressDialog, &QProgressDialog::canceled, [this]() {
        if (m_trimmer) {
            m_trimmer->cancel();
        }
    });

    // Start trimming
    m_trimmer->startTrim();
}
