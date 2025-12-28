#include "RecordingControlBar.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QtMath>

RecordingControlBar::RecordingControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_audioIndicator(nullptr)
    , m_sizeLabel(nullptr)
    , m_fpsLabel(nullptr)
    , m_audioEnabled(false)
    , m_hoveredButton(ButtonNone)
    , m_isDragging(false)
    , m_gradientOffset(0.0)
    , m_indicatorVisible(true)
    , m_isPaused(false)
    , m_isPreparing(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setFixedHeight(TOOLBAR_HEIGHT);

    setupUi();

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_gradientOffset += 0.02;
        if (m_gradientOffset > 1.0)
            m_gradientOffset -= 1.0;
        updateIndicatorGradient();
    });
    m_blinkTimer->start(16);

    m_escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_escShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_escShortcut, &QShortcut::activated, this, &RecordingControlBar::stopRequested);
}

RecordingControlBar::~RecordingControlBar()
{
}

void RecordingControlBar::setupUi()
{
    // Load icons for buttons
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("pause", ":/icons/icons/pause.svg");
    iconRenderer.loadIcon("play", ":/icons/icons/play.svg");
    iconRenderer.loadIcon("stop", ":/icons/icons/stop.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(6);

    // Recording indicator (animated gradient circle)
    m_recordingIndicator = new QLabel(this);
    m_recordingIndicator->setFixedSize(12, 12);
    updateIndicatorGradient();
    layout->addWidget(m_recordingIndicator);

    // Audio indicator (microphone icon, hidden by default)
    m_audioIndicator = new QLabel(this);
    m_audioIndicator->setFixedSize(12, 12);
    m_audioIndicator->setToolTip("Audio recording enabled");
    m_audioIndicator->hide();
    layout->addWidget(m_audioIndicator);

    // Duration label
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setStyleSheet(
        "color: white; font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_durationLabel->setMinimumWidth(55);
    layout->addWidget(m_durationLabel);

    // Separator
    QLabel *sep1 = new QLabel("|", this);
    sep1->setStyleSheet("color: rgba(255, 255, 255, 0.3); font-size: 11px;");
    layout->addWidget(sep1);

    // Size label
    m_sizeLabel = new QLabel("--", this);
    m_sizeLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.8); font-size: 10px; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_sizeLabel->setMinimumWidth(70);
    layout->addWidget(m_sizeLabel);

    // Separator
    QLabel *sep2 = new QLabel("|", this);
    sep2->setStyleSheet("color: rgba(255, 255, 255, 0.3); font-size: 11px;");
    layout->addWidget(sep2);

    // FPS label
    m_fpsLabel = new QLabel("-- fps", this);
    m_fpsLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.8); font-size: 10px; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_fpsLabel->setMinimumWidth(45);
    layout->addWidget(m_fpsLabel);

    // Add stretch to push buttons to the right
    layout->addStretch();

    // Reserve space for icon buttons (will be drawn in paintEvent)
    // 3 buttons: pause/resume, stop, cancel
    int buttonsWidth = 3 * BUTTON_WIDTH + 2 * BUTTON_SPACING + 4;
    QWidget *buttonSpacer = new QWidget(this);
    buttonSpacer->setFixedWidth(buttonsWidth);
    layout->addWidget(buttonSpacer);

    adjustSize();
    updateButtonRects();
}

void RecordingControlBar::updateButtonRects()
{
    int rightMargin = 10;
    int y = (height() - BUTTON_WIDTH + 4) / 2;

    // Buttons from right to left: cancel, stop, pause
    int x = width() - rightMargin - BUTTON_WIDTH;
    m_cancelRect = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);

    x -= BUTTON_WIDTH + BUTTON_SPACING;
    m_stopRect = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);

    x -= BUTTON_WIDTH + BUTTON_SPACING;
    m_pauseRect = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
}

void RecordingControlBar::updateIndicatorGradient()
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_isPreparing) {
        int alpha = 128 + static_cast<int>(64 * qSin(m_gradientOffset * 2 * M_PI));
        painter.setBrush(QColor(180, 180, 180, alpha));
    } else if (m_isPaused) {
        painter.setBrush(QColor(255, 184, 0));
    } else {
        QConicalGradient gradient(6, 6, 360 * m_gradientOffset);
        gradient.setColorAt(0.0, QColor(0, 122, 255));
        gradient.setColorAt(0.33, QColor(88, 86, 214));
        gradient.setColorAt(0.66, QColor(175, 82, 222));
        gradient.setColorAt(1.0, QColor(0, 122, 255));
        painter.setBrush(gradient);
    }

    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, 12, 12);
    painter.end();

    m_recordingIndicator->setPixmap(pixmap);
}

void RecordingControlBar::setPaused(bool paused)
{
    m_isPaused = paused;
    updateIndicatorGradient();
    update();
}

void RecordingControlBar::setPreparing(bool preparing)
{
    m_isPreparing = preparing;
    updateIndicatorGradient();

    if (preparing) {
        m_durationLabel->setText(tr("Preparing..."));
        m_durationLabel->setStyleSheet(
            "color: rgba(255, 255, 255, 0.7); font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    } else {
        m_durationLabel->setText("00:00:00");
        m_durationLabel->setStyleSheet(
            "color: white; font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    }

    adjustSize();
    updateButtonRects();
    update();
}

void RecordingControlBar::setPreparingStatus(const QString &status)
{
    if (m_isPreparing && m_durationLabel) {
        m_durationLabel->setText(status);
    }
}

void RecordingControlBar::updateRegionSize(int width, int height)
{
    if (m_sizeLabel) {
        m_sizeLabel->setText(QString("%1×%2").arg(width).arg(height));
    }
}

void RecordingControlBar::updateFps(double fps)
{
    if (m_fpsLabel) {
        m_fpsLabel->setText(QString("%1 fps").arg(qRound(fps)));
    }
}

void RecordingControlBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    QRect bgRect = rect().adjusted(1, 1, -1, -1);

    GlassRenderer::drawGlassPanel(painter, bgRect, config, 10);

    updateButtonRects();
    drawButtons(painter);
    drawTooltip(painter);
}

void RecordingControlBar::drawButtons(QPainter &painter)
{
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Draw pause/resume button
    if (!m_isPreparing) {
        bool isHovered = (m_hoveredButton == ButtonPause);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_pauseRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QString iconKey = m_isPaused ? "play" : "pause";
        QColor iconColor = isHovered ? config.iconActiveColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_pauseRect, iconKey, iconColor);
    }

    // Draw stop button
    if (!m_isPreparing) {
        bool isHovered = (m_hoveredButton == ButtonStop);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_stopRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = config.iconRecordColor;
        IconRenderer::instance().renderIcon(painter, m_stopRect, "stop", iconColor);
    }

    // Draw cancel button
    {
        bool isHovered = (m_hoveredButton == ButtonCancel);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_cancelRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = isHovered ? config.iconCancelColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_cancelRect, "cancel", iconColor);
    }
}

void RecordingControlBar::drawTooltip(QPainter &painter)
{
    if (m_hoveredButton == ButtonNone) return;

    QString tooltip = tooltipForButton(static_cast<ButtonId>(m_hoveredButton));
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    QRect btnRect;
    switch (m_hoveredButton) {
    case ButtonPause: btnRect = m_pauseRect; break;
    case ButtonStop: btnRect = m_stopRect; break;
    case ButtonCancel: btnRect = m_cancelRect; break;
    default: return;
    }

    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = -textRect.height() - 6;

    if (tooltipX < 5) tooltipX = 5;
    if (tooltipX + textRect.width() > width() - 5) {
        tooltipX = width() - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    ToolbarStyleConfig tooltipConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    tooltipConfig.shadowOffsetY = 2;
    tooltipConfig.shadowBlurRadius = 6;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 6);

    painter.setPen(tooltipConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

QString RecordingControlBar::tooltipForButton(ButtonId button) const
{
    switch (button) {
    case ButtonPause:
        return m_isPaused ? tr("Resume Recording") : tr("Pause Recording");
    case ButtonStop:
        return tr("Stop Recording");
    case ButtonCancel:
        return tr("Cancel Recording (Esc)");
    default:
        return QString();
    }
}

int RecordingControlBar::buttonAtPosition(const QPoint &pos) const
{
    if (!m_isPreparing && m_pauseRect.contains(pos)) return ButtonPause;
    if (!m_isPreparing && m_stopRect.contains(pos)) return ButtonStop;
    if (m_cancelRect.contains(pos)) return ButtonCancel;
    return ButtonNone;
}

void RecordingControlBar::positionNear(const QRect &recordingRegion)
{
    QScreen *screen = QGuiApplication::screenAt(recordingRegion.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();
    int x, y;

    bool isFullScreen = (recordingRegion == screenGeom);

    if (isFullScreen) {
        x = screenGeom.center().x() - width() / 2;
        y = screenGeom.bottom() - height() - 60;
    } else {
        x = recordingRegion.center().x() - width() / 2;
        y = recordingRegion.bottom() + 20;

        if (y + height() > screenGeom.bottom() - 10) {
            y = recordingRegion.top() - height() - 20;
        }

        if (y < screenGeom.top() + 10) {
            y = recordingRegion.top() + 10;
        }
    }

    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

    move(x, y);
}

void RecordingControlBar::updateDuration(qint64 elapsedMs)
{
    m_durationLabel->setText(formatDuration(elapsedMs));
}

QString RecordingControlBar::formatDuration(qint64 ms) const
{
    int hours = ms / 3600000;
    int minutes = (ms % 3600000) / 60000;
    int seconds = (ms % 60000) / 1000;

    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void RecordingControlBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int button = buttonAtPosition(event->pos());
        if (button != ButtonNone) {
            switch (button) {
            case ButtonPause:
                if (m_isPaused) {
                    emit resumeRequested();
                } else {
                    emit pauseRequested();
                }
                break;
            case ButtonStop:
                emit stopRequested();
                break;
            case ButtonCancel:
                emit cancelRequested();
                break;
            }
            return;
        }

        // Start dragging if not on a button
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void RecordingControlBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        QPoint newPos = m_dragStartWidgetPos + delta;

        QScreen *screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
        if (screen) {
            QRect screenGeom = screen->geometry();
            newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - width()));
            newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - height()));
        }

        move(newPos);
    } else {
        int newHovered = buttonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            if (m_hoveredButton != ButtonNone) {
                setCursor(Qt::PointingHandCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void RecordingControlBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        int button = buttonAtPosition(event->pos());
        if (button != ButtonNone) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void RecordingControlBar::leaveEvent(QEvent *event)
{
    if (m_hoveredButton != ButtonNone) {
        m_hoveredButton = ButtonNone;
        setCursor(Qt::ArrowCursor);
        update();
    }
    QWidget::leaveEvent(event);
}

void RecordingControlBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit stopRequested();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void RecordingControlBar::setAudioEnabled(bool enabled)
{
    m_audioEnabled = enabled;

    if (enabled) {
        QPixmap micIcon(12, 12);
        micIcon.fill(Qt::transparent);

        QPainter painter(&micIcon);
        painter.setRenderHint(QPainter::Antialiasing);

        QPen pen(QColor(52, 199, 89));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        painter.drawEllipse(3, 1, 6, 7);
        painter.drawLine(6, 8, 6, 10);
        painter.drawArc(2, 6, 8, 5, 0, -180 * 16);

        m_audioIndicator->setPixmap(micIcon);
        m_audioIndicator->show();
    } else {
        m_audioIndicator->hide();
    }
}
