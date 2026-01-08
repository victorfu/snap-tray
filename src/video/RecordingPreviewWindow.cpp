#include "video/RecordingPreviewWindow.h"
#include "video/VideoPlaybackWidget.h"
#include "video/TrimTimeline.h"
#include "video/VideoTrimmer.h"
#include "video/FormatSelectionWidget.h"
#include "video/VideoAnnotationEditor.h"
#include "video/AnnotationTrack.h"
#include "video/VideoAnnotationRenderer.h"
#include "encoding/EncoderFactory.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"
#include "ToolbarStyle.h"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QStackedWidget>
#include <QTimer>
#include <QComboBox>
#include <QDebug>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QString rgbaString(const QColor &color)
{
    return QString("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

bool isAnnotateEnabled()
{
#ifndef QT_NO_DEBUG
    return true;
#else
    return false;
#endif
}
}

class GlassPanel : public QWidget
{
public:
    explicit GlassPanel(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_cornerRadius(10)
    {
        // Don't use setAutoFillBackground - we handle background explicitly in paintEvent
        // This is required for proper rendering on Windows
        setAutoFillBackground(false);
    }

    void setCornerRadius(int radius)
    {
        if (m_cornerRadius != radius) {
            m_cornerRadius = radius;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

        // CRITICAL for Windows: Fill background explicitly FIRST before any other painting
        painter.fillRect(rect(), config.glassBackgroundColor);

        // Then draw glass panel effect on top
        GlassRenderer::drawGlassPanel(painter, rect(), config, m_cornerRadius);
    }

private:
    int m_cornerRadius;
};

class PreviewIconButton : public QAbstractButton
{
public:
    explicit PreviewIconButton(const QString &iconKey, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_iconKey(iconKey)
        , m_hovered(false)
        , m_useCustomIconColor(false)
        , m_cornerRadius(6)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setIconKey(const QString &key)
    {
        if (m_iconKey != key) {
            m_iconKey = key;
            updateGeometry();
            update();
        }
    }

    void setIconColor(const QColor &color)
    {
        m_iconColor = color;
        m_useCustomIconColor = true;
        update();
    }

    void clearIconColor()
    {
        m_useCustomIconColor = false;
        update();
    }

    QSize sizeHint() const override
    {
        return QSize(32, 32);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        const bool active = isDown() || isChecked();

        if (m_hovered || active) {
            QColor bg = active ? config.activeBackgroundColor : config.hoverBackgroundColor;
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), m_cornerRadius, m_cornerRadius);
        }

        QColor iconColor = m_useCustomIconColor ? m_iconColor
            : (m_hovered || active ? config.iconActiveColor : config.iconNormalColor);
        IconRenderer::instance().renderIcon(painter, rect(), m_iconKey, iconColor);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString m_iconKey;
    QColor m_iconColor;
    bool m_hovered;
    bool m_useCustomIconColor;
    int m_cornerRadius;
};

class PreviewVolumeButton : public QAbstractButton
{
public:
    explicit PreviewVolumeButton(QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_muted(false)
        , m_hovered(false)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setMuted(bool muted)
    {
        if (m_muted != muted) {
            m_muted = muted;
            update();
        }
    }

    bool isMuted() const
    {
        return m_muted;
    }

    QSize sizeHint() const override
    {
        return QSize(32, 32);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        if (m_hovered || isDown()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 6, 6);
        }

        QColor iconColor = m_hovered ? config.iconActiveColor : config.iconNormalColor;
        if (m_muted) {
            QRect iconRect = rect().adjusted(7, 7, -7, -7);
            painter.setPen(QPen(iconColor, 1.6));
            painter.drawLine(iconRect.topLeft(), iconRect.bottomRight());
            painter.drawLine(iconRect.topRight(), iconRect.bottomLeft());
        } else {
            QRect iconRect = rect().adjusted(8, 8, -8, -8);
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconColor);

            int barWidth = 3;
            int spacing = 2;
            int barCount = 3;
            int totalWidth = barCount * barWidth + (barCount - 1) * spacing;
            int startX = iconRect.center().x() - totalWidth / 2;

            for (int i = 0; i < barCount; ++i) {
                int barHeight = 4 + i * 3;
                int barY = iconRect.bottom() - barHeight;
                painter.drawRoundedRect(startX + i * (barWidth + spacing), barY,
                                        barWidth, barHeight, 1, 1);
            }
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    bool m_muted;
    bool m_hovered;
};

class PreviewPillButton : public QAbstractButton
{
public:
    explicit PreviewPillButton(const QString &text, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_hovered(false)
        , m_useAccentColor(false)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }

    void setIconKey(const QString &key)
    {
        if (m_iconKey != key) {
            m_iconKey = key;
            update();
        }
    }

    void setAccentColor(const QColor &color)
    {
        m_accentColor = color;
        m_useAccentColor = true;
        update();
    }

    QSize sizeHint() const override
    {
        QFont f = font();
        f.setPointSize(10);
        f.setWeight(QFont::Medium);
        QFontMetrics fm(f);
        int height = 28;
        int textWidth = fm.horizontalAdvance(text());
        int iconWidth = m_iconKey.isEmpty() ? 0 : (height - 12) + 6;
        int width = textWidth + iconWidth + 24;
        return QSize(width, height);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        const bool active = isDown() || isChecked();

        QColor bg = config.buttonInactiveColor;
        bg.setAlpha(120);
        if (m_hovered) {
            bg = config.buttonHoverColor;
        }
        if (active) {
            bg = m_useAccentColor ? m_accentColor : config.buttonActiveColor;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

        painter.setPen(QPen(config.hairlineBorderColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

        QFont font = painter.font();
        font.setPointSize(10);
        font.setWeight(QFont::Medium);
        painter.setFont(font);

        QColor textColor = active ? config.textActiveColor : config.textColor;
        painter.setPen(textColor);

        int leftPadding = 10;
        QRect textRect = rect().adjusted(leftPadding, 0, -10, 0);

        if (!m_iconKey.isEmpty()) {
            int iconSize = height() - 12;
            QRect iconRect(rect().left() + leftPadding,
                           rect().center().y() - iconSize / 2,
                           iconSize, iconSize);
            IconRenderer::instance().renderIcon(painter, iconRect, m_iconKey, textColor);
            textRect.setLeft(iconRect.right() + 6);
        }

        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text());
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString m_iconKey;
    QColor m_accentColor;
    bool m_hovered;
    bool m_useAccentColor;
};

// Annotation overlay for main preview mode
class PreviewAnnotationOverlay : public QWidget
{
public:
    explicit PreviewAnnotationOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setTrack(AnnotationTrack *track)
    {
        m_track = track;
        update();
    }

    void setCurrentTime(qint64 timeMs)
    {
        if (m_currentTimeMs != timeMs) {
            m_currentTimeMs = timeMs;
            update();
        }
    }

    void setDuration(qint64 durationMs)
    {
        m_durationMs = durationMs;
    }

    void setVideoSize(const QSize &size)
    {
        m_videoSize = size;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (!m_track || m_track->isEmpty() || m_videoSize.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        m_renderer.renderToWidget(painter, m_track, m_currentTimeMs, m_durationMs,
                                  rect(), m_videoSize);
    }

private:
    AnnotationTrack *m_track = nullptr;
    VideoAnnotationRenderer m_renderer;
    qint64 m_currentTimeMs = 0;
    qint64 m_durationMs = 0;
    QSize m_videoSize;
};

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
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("play", ":/icons/icons/play.svg");
    iconRenderer.loadIcon("pause", ":/icons/icons/pause.svg");
    iconRenderer.loadIcon("save", ":/icons/icons/save.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    m_stackedWidget = new QStackedWidget(this);
    mainLayout->addWidget(m_stackedWidget);

    m_previewModeWidget = new QWidget(m_stackedWidget);
    QVBoxLayout *previewLayout = new QVBoxLayout(m_previewModeWidget);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(12);
    m_stackedWidget->addWidget(m_previewModeWidget);
    m_stackedWidget->setCurrentWidget(m_previewModeWidget);

    // Video widget
    m_videoWidget = new VideoPlaybackWidget(m_previewModeWidget);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLayout->addWidget(m_videoWidget, 1);

    // Create annotation track (shared between preview and editor)
    m_annotationTrack = new AnnotationTrack(this);

    // Create annotation overlay on top of video widget
    m_annotationOverlay = new PreviewAnnotationOverlay(m_videoWidget);
    m_annotationOverlay->setTrack(m_annotationTrack);
    m_annotationOverlay->setGeometry(m_videoWidget->rect());

    // Install event filter to handle video widget resize
    m_videoWidget->installEventFilter(this);

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

    // Timeline panel with trim handles
    GlassPanel *timelinePanel = new GlassPanel(m_previewModeWidget);
    timelinePanel->setCornerRadius(12);
    timelinePanel->setMinimumHeight(44);
    QHBoxLayout *timelineLayout = new QHBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(12, 6, 12, 6);
    timelineLayout->setSpacing(10);

    m_timeline = new TrimTimeline(timelinePanel);
    timelineLayout->addWidget(m_timeline, 1);
    previewLayout->addWidget(timelinePanel);

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

    m_trimPreviewToggle = new PreviewPillButton(tr("Trim"), timelinePanel);
    m_trimPreviewToggle->setCheckable(true);
    m_trimPreviewToggle->setFixedHeight(28);
    m_trimPreviewToggle->setToolTip(tr("Loop playback within the trimmed range"));
    m_trimPreviewEnabled = false;
    connect(m_trimPreviewToggle, &QAbstractButton::toggled,
            this, &RecordingPreviewWindow::onTrimPreviewToggled);
    timelineLayout->addWidget(m_trimPreviewToggle);

    // Row 1: Playback controls (glass panel)
    GlassPanel *row1Panel = new GlassPanel(m_previewModeWidget);
    row1Panel->setCornerRadius(12);
    row1Panel->setMinimumHeight(44);
    QHBoxLayout *row1Layout = new QHBoxLayout(row1Panel);
    row1Layout->setContentsMargins(12, 6, 12, 6);
    row1Layout->setSpacing(8);

    m_playPauseBtn = new PreviewIconButton("play", row1Panel);
    m_playPauseBtn->setFixedSize(32, 32);
    m_playPauseBtn->setToolTip(tr("Play/Pause (Space)"));
    connect(m_playPauseBtn, &QAbstractButton::clicked,
            this, &RecordingPreviewWindow::onPlayPauseClicked);
    row1Layout->addWidget(m_playPauseBtn);

    m_timeLabel = new QLabel("00:00 / 00:00", row1Panel);
    QFont timeFont("SF Mono", 11);
    timeFont.setWeight(QFont::DemiBold);
    m_timeLabel->setFont(timeFont);
    m_timeLabel->setAlignment(Qt::AlignCenter);
    m_timeLabel->setMinimumWidth(110);
    row1Layout->addWidget(m_timeLabel);

    m_speedCombo = new QComboBox(row1Panel);
    m_speedCombo->addItem("0.25x", 0.25f);
    m_speedCombo->addItem("0.5x", 0.5f);
    m_speedCombo->addItem("0.75x", 0.75f);
    m_speedCombo->addItem("1.0x", 1.0f);
    m_speedCombo->addItem("1.25x", 1.25f);
    m_speedCombo->addItem("1.5x", 1.5f);
    m_speedCombo->addItem("2.0x", 2.0f);
    m_speedCombo->setCurrentIndex(kDefaultSpeedIndex);
    m_speedCombo->setFixedWidth(90);
    m_speedCombo->setFixedHeight(28);
    m_speedCombo->setToolTip(tr("Playback speed"));
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RecordingPreviewWindow::onSpeedChanged);
    row1Layout->addWidget(m_speedCombo);

    row1Layout->addStretch();

    m_muteBtn = new PreviewVolumeButton(row1Panel);
    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setToolTip(tr("Mute (M)"));
    connect(m_muteBtn, &QAbstractButton::clicked,
            this, &RecordingPreviewWindow::onMuteToggled);
    row1Layout->addWidget(m_muteBtn);

    m_volumeSlider = new QSlider(Qt::Horizontal, row1Panel);
    m_volumeSlider->setMinimum(0);
    m_volumeSlider->setMaximum(100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(110);
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &RecordingPreviewWindow::onVolumeChanged);
    row1Layout->addWidget(m_volumeSlider);

    previewLayout->addWidget(row1Panel);

    // Row 2: Action controls (glass panel)
    GlassPanel *row2Panel = new GlassPanel(m_previewModeWidget);
    row2Panel->setCornerRadius(12);
    row2Panel->setMinimumHeight(44);
    QHBoxLayout *row2Layout = new QHBoxLayout(row2Panel);
    row2Layout->setContentsMargins(12, 6, 12, 6);
    row2Layout->setSpacing(8);

    m_formatWidget = new FormatSelectionWidget(row2Panel);
    row2Layout->addWidget(m_formatWidget);

    m_annotateBtn = new PreviewPillButton(tr("Annotate"), row2Panel);
    m_annotateBtn->setIconKey("pencil");
    m_annotateBtn->setFixedHeight(28);
    m_annotateBtn->setToolTip(tr("Add annotations to the video"));
    connect(m_annotateBtn, &QAbstractButton::clicked, this, &RecordingPreviewWindow::switchToAnnotateMode);
    const bool annotateEnabled = isAnnotateEnabled();
    m_annotateBtn->setEnabled(annotateEnabled);
    m_annotateBtn->setVisible(annotateEnabled);
    row2Layout->addWidget(m_annotateBtn);

    row2Layout->addStretch();

    m_discardBtn = new PreviewIconButton("cancel", row2Panel);
    m_discardBtn->setFixedSize(32, 32);
    m_discardBtn->setToolTip(tr("Discard (Esc)"));
    connect(m_discardBtn, &QAbstractButton::clicked,
            this, &RecordingPreviewWindow::onDiscardClicked);
    row2Layout->addWidget(m_discardBtn);

    m_saveBtn = new PreviewIconButton("save", row2Panel);
    m_saveBtn->setFixedSize(32, 32);
    m_saveBtn->setToolTip(tr("Save (Enter)"));
    m_saveBtn->setIconColor(QColor(52, 199, 89));
    connect(m_saveBtn, &QAbstractButton::clicked,
            this, &RecordingPreviewWindow::onSaveClicked);
    row2Layout->addWidget(m_saveBtn);

    previewLayout->addWidget(row2Panel);

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    m_timeLabel->setStyleSheet(QString("color: %1;").arg(rgbaString(config.textColor)));
    m_discardBtn->setIconColor(config.iconCancelColor);

    QColor comboBg = config.buttonInactiveColor;
    comboBg.setAlpha(180);
    QColor comboHover = config.buttonHoverColor;
    comboHover.setAlpha(200);
    QString comboStyle = QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 2px 4px 2px 8px; }"
        "QComboBox:hover { background-color: %4; }"
        "QComboBox::drop-down { border: none; width: 20px; subcontrol-position: right center; }"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid %2; margin-right: 6px; }"
        "QComboBox QAbstractItemView { background-color: %5; color: %2; border: 1px solid %3; selection-background-color: %6; padding: 4px; }"
    )
        .arg(rgbaString(comboBg))
        .arg(rgbaString(config.textColor))
        .arg(rgbaString(config.hairlineBorderColor))
        .arg(rgbaString(comboHover))
        .arg(rgbaString(config.dropdownBackground))
        .arg(rgbaString(config.buttonActiveColor));
    m_speedCombo->setStyleSheet(comboStyle);

    QColor grooveColor = config.hoverBackgroundColor;
    grooveColor.setAlpha(160);
    QString sliderStyle = QString(
        "QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: white; border: 1px solid %3; border-radius: 6px; }"
    )
        .arg(rgbaString(grooveColor))
        .arg(rgbaString(config.buttonActiveColor))
        .arg(rgbaString(config.hairlineBorderColor));
    m_volumeSlider->setStyleSheet(sliderStyle);

    // Initial button states
    updatePlayPauseButton();
    updateTimeLabel();
    m_muteBtn->setMuted(m_videoWidget->isMuted());
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

void RecordingPreviewWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    QLinearGradient gradient(rect().topLeft(), rect().bottomLeft());
    gradient.setColorAt(0.0, config.backgroundColorTop);
    gradient.setColorAt(1.0, config.backgroundColorBottom);

    painter.fillRect(rect(), gradient);
}

bool RecordingPreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_videoWidget && event->type() == QEvent::Resize) {
        if (m_annotationOverlay) {
            m_annotationOverlay->setGeometry(m_videoWidget->rect());
            m_annotationOverlay->raise();
        }
    }
    return QWidget::eventFilter(watched, event);
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

    // Update annotation overlay
    if (m_annotationOverlay) {
        m_annotationOverlay->setCurrentTime(positionMs);
    }
}

void RecordingPreviewWindow::onDurationChanged(qint64 durationMs)
{
    m_duration = durationMs;
    m_timeline->setDuration(durationMs);
    updateTimeLabel();

    // Update annotation overlay
    if (m_annotationOverlay) {
        m_annotationOverlay->setDuration(durationMs);
    }
}

void RecordingPreviewWindow::onVideoLoaded()
{
    qDebug() << "RecordingPreviewWindow: Video loaded, duration:" << m_videoWidget->duration();

    // Check if this is a GIF (by extension) and enable looping
    if (m_videoPath.endsWith(".gif", Qt::CaseInsensitive)) {
        m_videoWidget->setLooping(true);
        qDebug() << "RecordingPreviewWindow: GIF detected, looping enabled";
    }

    // Update annotation overlay with video size
    if (m_annotationOverlay) {
        m_annotationOverlay->setVideoSize(m_videoWidget->videoSize());
        m_annotationOverlay->setGeometry(m_videoWidget->rect());
        m_annotationOverlay->raise();
    }

    // Auto-play on load
    m_videoWidget->play();
    updatePlayPauseButton();
    m_muteBtn->setMuted(m_videoWidget->isMuted());
    m_volumeSlider->setValue(static_cast<int>(m_videoWidget->volume() * 100));

    // Load any existing annotations
    loadAnnotations();
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

    // Save annotations to sidecar file
    saveAnnotations();

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

    m_muteBtn->setMuted(m_videoWidget->isMuted());
}

void RecordingPreviewWindow::onMuteToggled()
{
    bool muted = !m_videoWidget->isMuted();
    m_videoWidget->setMuted(muted);
    m_muteBtn->setMuted(muted);

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
    m_playPauseBtn->setIconKey(isPlaying ? "pause" : "play");
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

// ============================================================================
// Mode switching implementations
// ============================================================================

void RecordingPreviewWindow::switchToAnnotateMode()
{
    if (!isAnnotateEnabled()) {
        qInfo() << "RecordingPreviewWindow: Annotate mode disabled in non-debug builds";
        return;
    }
    if (m_currentMode == Mode::Annotate) {
        return;
    }

    qDebug() << "RecordingPreviewWindow: Switching to annotate mode";
    m_currentMode = Mode::Annotate;

    // Pause video playback
    m_videoWidget->pause();

    // Create annotation editor if not already created
    if (!m_annotationEditor) {
        m_annotationEditor = new VideoAnnotationEditor(m_stackedWidget);
        // Set track BEFORE video path to avoid race condition during async video loading
        m_annotationEditor->setTrack(m_annotationTrack);
        m_annotationEditor->setVideoPath(m_videoPath);

        // Create done editing button
        m_doneEditingBtn = new PreviewPillButton(tr("Done Editing"), m_annotationEditor);
        m_doneEditingBtn->setFixedHeight(28);
        m_doneEditingBtn->setToolTip(tr("Back to preview"));
        connect(m_doneEditingBtn, &QAbstractButton::clicked, this, &RecordingPreviewWindow::switchToPreviewMode);

        // Add button to annotation editor layout
        if (QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(m_annotationEditor->layout())) {
            QHBoxLayout *buttonLayout = new QHBoxLayout();
            buttonLayout->addStretch();
            buttonLayout->addWidget(m_doneEditingBtn);
            layout->addLayout(buttonLayout);
        }
    }

    if (m_stackedWidget) {
        if (m_stackedWidget->indexOf(m_annotationEditor) == -1) {
            m_stackedWidget->addWidget(m_annotationEditor);
        }
        m_stackedWidget->setCurrentWidget(m_annotationEditor);
        m_annotationEditor->setFocus(Qt::OtherFocusReason);
    } else {
        m_annotationEditor->show();
    }

    setWindowTitle("Recording Preview - Annotation Editor");
}

void RecordingPreviewWindow::switchToPreviewMode()
{
    if (m_currentMode == Mode::Preview) {
        return;
    }

    qDebug() << "RecordingPreviewWindow: Switching to preview mode";
    m_currentMode = Mode::Preview;

    if (m_stackedWidget && m_previewModeWidget) {
        m_stackedWidget->setCurrentWidget(m_previewModeWidget);
    } else if (m_annotationEditor) {
        m_annotationEditor->hide();
    }

    // Update annotation overlay with current video size and position
    if (m_annotationOverlay) {
        m_annotationOverlay->setVideoSize(m_videoWidget->videoSize());
        m_annotationOverlay->setGeometry(m_videoWidget->rect());
        m_annotationOverlay->raise();
        m_annotationOverlay->update();
    }

    setWindowTitle("Recording Preview");
}

// ============================================================================
// Annotation persistence
// ============================================================================

QString RecordingPreviewWindow::annotationFilePath() const
{
    return m_videoPath + ".annotations.json";
}

void RecordingPreviewWindow::saveAnnotations()
{
    if (!m_annotationTrack || m_annotationTrack->isEmpty()) {
        return;
    }

    QString path = annotationFilePath();
    QJsonObject root;
    root["version"] = 1;
    root["videoPath"] = QFileInfo(m_videoPath).fileName();
    root["annotations"] = m_annotationTrack->toJson();

    QJsonDocument doc(root);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        qDebug() << "RecordingPreviewWindow: Saved annotations to" << path;
    } else {
        qWarning() << "RecordingPreviewWindow: Failed to save annotations to" << path;
    }
}

void RecordingPreviewWindow::loadAnnotations()
{
    QString path = annotationFilePath();
    if (!QFile::exists(path)) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "RecordingPreviewWindow: Failed to open annotations file" << path;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "RecordingPreviewWindow: Invalid annotations file format";
        return;
    }

    QJsonObject root = doc.object();
    if (root.contains("annotations") && m_annotationTrack) {
        m_annotationTrack->fromJson(root["annotations"].toObject());
        qDebug() << "RecordingPreviewWindow: Loaded annotations from" << path
                 << "count:" << m_annotationTrack->allAnnotations().size();

        // Update overlay
        if (m_annotationOverlay) {
            m_annotationOverlay->update();
        }
    }
}
