#include "scrolling/ScrollingCaptureThumbnail.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include <QPainter>
#include <QMouseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>

// Colors for glass panel UI (always uses light text on dark translucent background)
struct GlassPanelColors {
    QColor text;
    QColor textSecondary;
    QColor viewportBg;
    QColor errorText;
};

static GlassPanelColors getGlassPanelColors() {
    // Glass panels have dark translucent backgrounds, so always use light text
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    return {
        styleConfig.textColor,                    // text - from style config
        styleConfig.textColor.darker(130),        // textSecondary - slightly dimmer
        QColor(0, 0, 0, 60),                      // viewportBg - semi-transparent dark
        QColor("#FF6B6B")                         // errorText - bright red for visibility
    };
}

// Status colors
static const QMap<ScrollingCaptureThumbnail::CaptureStatus, QColor> STATUS_COLORS = {
    {ScrollingCaptureThumbnail::CaptureStatus::Idle,      QColor("#888888")},
    {ScrollingCaptureThumbnail::CaptureStatus::Capturing, QColor("#4CAF50")},
    {ScrollingCaptureThumbnail::CaptureStatus::Warning,   QColor("#FFC107")},
    {ScrollingCaptureThumbnail::CaptureStatus::Failed,    QColor("#FF3B30")},
    {ScrollingCaptureThumbnail::CaptureStatus::Recovered, QColor("#007AFF")}
};

// Scroll speed colors and labels
static const QMap<ScrollingCaptureThumbnail::ScrollSpeed, QColor> SPEED_COLORS = {
    {ScrollingCaptureThumbnail::ScrollSpeed::Idle,    QColor("#888888")},
    {ScrollingCaptureThumbnail::ScrollSpeed::TooSlow, QColor("#64B5F6")},  // Blue
    {ScrollingCaptureThumbnail::ScrollSpeed::Good,    QColor("#4CAF50")},  // Green
    {ScrollingCaptureThumbnail::ScrollSpeed::TooFast, QColor("#FFC107")}   // Yellow/Orange
};

static const QMap<ScrollingCaptureThumbnail::ScrollSpeed, QString> SPEED_LABELS = {
    {ScrollingCaptureThumbnail::ScrollSpeed::Idle,    "—"},
    {ScrollingCaptureThumbnail::ScrollSpeed::TooSlow, "Slow"},
    {ScrollingCaptureThumbnail::ScrollSpeed::Good,    "Good"},
    {ScrollingCaptureThumbnail::ScrollSpeed::TooFast, "Fast"}
};

ScrollingCaptureThumbnail::ScrollingCaptureThumbnail(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);

    setupUI();
    applyThemeColors();

    // Throttling timer
    m_viewportThrottleTimer = new QTimer(this);
    m_viewportThrottleTimer->setSingleShot(true);
    connect(m_viewportThrottleTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingViewportImage.isNull()) {
            applyViewportImage(m_pendingViewportImage);
        }
    });
}

ScrollingCaptureThumbnail::~ScrollingCaptureThumbnail()
{
}

void ScrollingCaptureThumbnail::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // Viewport Preview
    m_viewportLabel = new QLabel(this);
    m_viewportLabel->setAlignment(Qt::AlignCenter);
    m_viewportLabel->setFixedSize(200, 120); // Fixed size for consistency
    mainLayout->addWidget(m_viewportLabel);

    // Status Row (status on left, speed on right)
    QHBoxLayout* statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel("Ready", this);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    m_speedLabel = new QLabel("—", this);
    statusLayout->addWidget(m_speedLabel);
    mainLayout->addLayout(statusLayout);

    // Stats
    m_statsLabel = new QLabel("0 frames • 0 x 0 px", this);
    mainLayout->addWidget(m_statsLabel);

    // Error Section (Hidden by default)
    m_errorSection = new QWidget(this);
    QVBoxLayout* errorLayout = new QVBoxLayout(m_errorSection);
    errorLayout->setContentsMargins(0, 4, 0, 0);
    errorLayout->setSpacing(2);

    m_errorTitleLabel = new QLabel(this);
    m_errorTitleLabel->setWordWrap(true);
    errorLayout->addWidget(m_errorTitleLabel);

    m_errorHintLabel = new QLabel(this);
    m_errorHintLabel->setWordWrap(true);
    errorLayout->addWidget(m_errorHintLabel);

    m_errorSection->setVisible(false);
    mainLayout->addWidget(m_errorSection);

    // Set initial size
    resize(224, 200);
}

void ScrollingCaptureThumbnail::applyThemeColors()
{
    auto colors = getGlassPanelColors();

    // Use rgba for semi-transparent background
    m_viewportLabel->setStyleSheet(
        QString("background-color: rgba(%1, %2, %3, %4); border-radius: 4px;")
            .arg(colors.viewportBg.red())
            .arg(colors.viewportBg.green())
            .arg(colors.viewportBg.blue())
            .arg(colors.viewportBg.alpha()));

    m_statusLabel->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 12px;")
            .arg(colors.text.name()));

    m_statsLabel->setStyleSheet(
        QString("color: %1; font-size: 11px;")
            .arg(colors.textSecondary.name()));

    m_speedLabel->setStyleSheet(
        QString("color: %1; font-size: 11px;")
            .arg(colors.textSecondary.name()));

    m_errorTitleLabel->setStyleSheet(
        QString("color: %1; font-weight: bold; font-size: 11px;")
            .arg(colors.errorText.name()));

    m_errorHintLabel->setStyleSheet(
        QString("color: %1; font-size: 11px;")
            .arg(colors.errorText.name()));
}

void ScrollingCaptureThumbnail::setViewportImage(const QImage& image)
{
    if (m_lastViewportUpdate.isValid() &&
        m_lastViewportUpdate.elapsed() < VIEWPORT_UPDATE_INTERVAL_MS) {
        m_pendingViewportImage = image;
        if (!m_viewportThrottleTimer->isActive()) {
            m_viewportThrottleTimer->start(VIEWPORT_UPDATE_INTERVAL_MS);
        }
        return;
    }
    applyViewportImage(image);
}

void ScrollingCaptureThumbnail::applyViewportImage(const QImage& image)
{
    if (image.isNull()) return;
    
    QPixmap pixmap = QPixmap::fromImage(image).scaled(
        m_viewportLabel->size(),
        Qt::KeepAspectRatio,
        Qt::FastTransformation  // Use fast scaling for real-time preview
    );
    m_viewportLabel->setPixmap(pixmap);
    
    m_pendingViewportImage = QImage();
    m_lastViewportUpdate.restart();
}

void ScrollingCaptureThumbnail::setStats(int frameCount, QSize totalSize)
{
    m_statsLabel->setText(QString("%1 frames • %2 x %3 px")
        .arg(frameCount)
        .arg(totalSize.width())
        .arg(totalSize.height()));
}

void ScrollingCaptureThumbnail::setStatus(CaptureStatus status, const QString& message)
{
    // Cancel any pending recovered timer
    if (m_recoveredTimer && m_recoveredTimer->isActive()) {
        m_recoveredTimer->stop();
    }
    
    // Transition Failed -> Capturing shows Recovered first
    if (m_currentStatus == CaptureStatus::Failed && status == CaptureStatus::Capturing) {
        m_pendingStatus = status;
        applyStatus(CaptureStatus::Recovered, "Fixed");
        
        if (!m_recoveredTimer) {
            m_recoveredTimer = new QTimer(this);
            m_recoveredTimer->setSingleShot(true);
            connect(m_recoveredTimer, &QTimer::timeout, this, [this]() {
                applyStatus(m_pendingStatus, "Active");
            });
        }
        m_recoveredTimer->start(1500);
        return;
    }
    
    applyStatus(status, message);
}

void ScrollingCaptureThumbnail::applyStatus(CaptureStatus status, const QString& message)
{
    m_currentStatus = status;
    QColor color = STATUS_COLORS.value(status, Qt::white);
    
    QString statusText = message.isEmpty() ? "Active" : message;
    if (status == CaptureStatus::Idle) statusText = "Idle";
    if (status == CaptureStatus::Failed) statusText = "Failed";
    if (status == CaptureStatus::Recovered) statusText = "Fixed";
    
    // Update indicator dot + text
    m_statusLabel->setText(QString("<font color='%1'>●</font> %2")
        .arg(color.name())
        .arg(statusText));
        
    update(); // Repaint glass border if needed
}

void ScrollingCaptureThumbnail::setErrorInfo(ImageStitcher::FailureCode code, const QString& debugReason, int recoveryDistancePx)
{
    m_errorSection->setVisible(true);

    ErrorInfo info = failureCodeToErrorInfo(code, recoveryDistancePx);
    m_errorTitleLabel->setText(QString::fromUtf8("\u26A0 %1").arg(info.title));  // ⚠
    m_errorHintLabel->setText(info.hint);

    m_errorSection->setToolTip(debugReason);

    // Adjust height
    adjustSize();
}

void ScrollingCaptureThumbnail::clearError()
{
    m_errorSection->setVisible(false);
    adjustSize();
}

void ScrollingCaptureThumbnail::setCaptureDirection(CaptureDirection direction)
{
    m_captureDirection = direction;
}

void ScrollingCaptureThumbnail::positionNear(const QRect &captureRegion)
{
    // Position to the right of capture region if possible, else left
    int x = captureRegion.right() + 20;
    int y = captureRegion.top();
    
    // Basic screen bounds check (using primary screen logic for simplicity)
    // In real app, check m_targetScreen
    if (parentWidget()) {
        if (x + width() > parentWidget()->width()) {
            x = captureRegion.left() - width() - 20;
        }
    }
    
    move(x, y);
}

void ScrollingCaptureThumbnail::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    auto config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, rect(), config);
}

void ScrollingCaptureThumbnail::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        event->accept();
    }
}

void ScrollingCaptureThumbnail::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
        event->accept();
    }
}

void ScrollingCaptureThumbnail::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
        event->accept();
    }
}

bool ScrollingCaptureThumbnail::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange) {
        applyThemeColors();
    }
    return QWidget::event(event);
}

ScrollingCaptureThumbnail::ErrorInfo ScrollingCaptureThumbnail::failureCodeToErrorInfo(
    ImageStitcher::FailureCode code, int recoveryPx) const
{
    // Direction arrow for recovery hint (vertical = scroll back up, horizontal = scroll back left)
    QString arrow = (m_captureDirection == CaptureDirection::Vertical)
        ? QString::fromUtf8("\u2191")   // ↑
        : QString::fromUtf8("\u2190");  // ←

    QString recoveryHint = QString("Scroll back %1 ~%2px").arg(arrow).arg(recoveryPx);

    switch (code) {
        case ImageStitcher::FailureCode::OverlapMismatch:
            return {"Lost track", recoveryHint};

        case ImageStitcher::FailureCode::AmbiguousMatch:
            return {"Repetitive content", "Vary scroll speed slightly"};

        case ImageStitcher::FailureCode::LowConfidence:
            return {"Uncertain match", "Slow down scroll speed"};

        case ImageStitcher::FailureCode::ViewportMismatch:
            return {"Window resized", "Keep window size unchanged"};

        case ImageStitcher::FailureCode::OverlapTooSmall:
            return {"Scrolled too fast", recoveryHint};

        case ImageStitcher::FailureCode::NoAlgorithmSucceeded:
            return {"Match failed", recoveryHint};

        case ImageStitcher::FailureCode::Timeout:
            return {"Processing slow", "Wait a moment"};

        case ImageStitcher::FailureCode::InvalidState:
            return {"Internal error", "Try again"};

        case ImageStitcher::FailureCode::DuplicateDetected:
            return {"Duplicate content", "Continue scrolling"};

        case ImageStitcher::FailureCode::MaxSizeReached:
            return {"Max size reached", "Capture will finish"};

        default:
            return {"Error", recoveryHint};
    }
}

void ScrollingCaptureThumbnail::setOverlapRatio(double ratio)
{
    // Convert overlap ratio to scroll speed category
    // Higher overlap = slower scrolling, lower overlap = faster scrolling
    ScrollSpeed speed;

    if (ratio > 0.80) {
        speed = ScrollSpeed::TooSlow;
    } else if (ratio >= 0.30) {
        speed = ScrollSpeed::Good;
    } else {
        speed = ScrollSpeed::TooFast;
    }

    if (speed != m_scrollSpeed) {
        m_scrollSpeed = speed;
        applyScrollSpeed(speed);
    }
}

void ScrollingCaptureThumbnail::applyScrollSpeed(ScrollSpeed speed)
{
    QColor color = SPEED_COLORS.value(speed, QColor("#888888"));
    QString label = SPEED_LABELS.value(speed, "—");

    m_speedLabel->setText(QString("<font color='%1'>%2</font>").arg(color.name()).arg(label));
}

// Compatibility method
void ScrollingCaptureThumbnail::setMatchStatus(MatchStatus status, double confidence)
{
    CaptureStatus newStatus = CaptureStatus::Capturing;
    if (status == MatchStatus::Warning) newStatus = CaptureStatus::Warning;
    if (status == MatchStatus::Failed) newStatus = CaptureStatus::Failed;
    setStatus(newStatus);
}