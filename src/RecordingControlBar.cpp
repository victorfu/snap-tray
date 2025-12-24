#include "RecordingControlBar.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QConicalGradient>
#include <QLinearGradient>

RecordingControlBar::RecordingControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_pauseButton(nullptr)
    , m_sizeLabel(nullptr)
    , m_fpsLabel(nullptr)
    , m_isDragging(false)
    , m_gradientOffset(0.0)
    , m_indicatorVisible(true)
    , m_isPaused(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);  // Don't steal focus
    setFixedHeight(48);

    setupUi();

    // Animation timer for indicator gradient
    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_gradientOffset += 0.02;
        if (m_gradientOffset > 1.0)
            m_gradientOffset -= 1.0;
        updateIndicatorGradient();
    });
    m_blinkTimer->start(16);  // ~60 FPS
}

RecordingControlBar::~RecordingControlBar()
{
}

void RecordingControlBar::setupUi()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(10);

    // Recording indicator (animated gradient circle)
    m_recordingIndicator = new QLabel(this);
    m_recordingIndicator->setFixedSize(16, 16);
    updateIndicatorGradient();
    layout->addWidget(m_recordingIndicator);

    // Duration label
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setStyleSheet(
        "color: white; font-size: 14px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_durationLabel->setMinimumWidth(75);
    layout->addWidget(m_durationLabel);

    // Separator
    QLabel *sep1 = new QLabel("|", this);
    sep1->setStyleSheet("color: rgba(255, 255, 255, 0.3); font-size: 14px;");
    layout->addWidget(sep1);

    // Size label
    m_sizeLabel = new QLabel("--", this);
    m_sizeLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.8); font-size: 12px; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_sizeLabel->setMinimumWidth(85);
    layout->addWidget(m_sizeLabel);

    // Separator
    QLabel *sep2 = new QLabel("|", this);
    sep2->setStyleSheet("color: rgba(255, 255, 255, 0.3); font-size: 14px;");
    layout->addWidget(sep2);

    // FPS label
    m_fpsLabel = new QLabel("-- fps", this);
    m_fpsLabel->setStyleSheet(
        "color: rgba(255, 255, 255, 0.8); font-size: 12px; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_fpsLabel->setMinimumWidth(55);
    layout->addWidget(m_fpsLabel);

    layout->addSpacing(8);

    // Pause/Resume button
    m_pauseButton = new QPushButton("Pause", this);
    updatePauseButton();
    connect(m_pauseButton, &QPushButton::clicked, this, [this]() {
        if (m_isPaused) {
            emit resumeRequested();
        } else {
            emit pauseRequested();
        }
    });
    layout->addWidget(m_pauseButton);

    // Stop button
    m_stopButton = new QPushButton("Stop", this);
    m_stopButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #FF3B30;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 7px 18px;"
        "  font-weight: 600;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #FF5544;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #CC2F26;"
        "}");
    connect(m_stopButton, &QPushButton::clicked, this, &RecordingControlBar::stopRequested);
    layout->addWidget(m_stopButton);

    // Cancel button
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "  color: white;"
        "  border: 1px solid rgba(255, 255, 255, 0.2);"
        "  border-radius: 6px;"
        "  padding: 7px 14px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 0.25);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.1);"
        "}");
    connect(m_cancelButton, &QPushButton::clicked, this, &RecordingControlBar::cancelRequested);
    layout->addWidget(m_cancelButton);

    adjustSize();
}

void RecordingControlBar::updateIndicatorGradient()
{
    // Create a pixmap with gradient
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_isPaused) {
        // Yellow when paused
        painter.setBrush(QColor(255, 184, 0));
    } else {
        // Animated gradient when recording
        QConicalGradient gradient(8, 8, 360 * m_gradientOffset);
        gradient.setColorAt(0.0, QColor(0, 122, 255));      // Blue
        gradient.setColorAt(0.33, QColor(88, 86, 214));     // Indigo
        gradient.setColorAt(0.66, QColor(175, 82, 222));    // Purple
        gradient.setColorAt(1.0, QColor(0, 122, 255));      // Blue
        painter.setBrush(gradient);
    }

    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, 16, 16);
    painter.end();

    m_recordingIndicator->setPixmap(pixmap);
}

void RecordingControlBar::setPaused(bool paused)
{
    m_isPaused = paused;
    updatePauseButton();
    updateIndicatorGradient();
}

void RecordingControlBar::updatePauseButton()
{
    if (m_isPaused) {
        // Show "Resume" button (green)
        m_pauseButton->setText("Resume");
        m_pauseButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #34C759;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 7px 14px;"
            "  font-weight: 600;"
            "  font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #30B350;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #2A9D48;"
            "}");
    } else {
        // Show "Pause" button (blue-purple gradient style)
        m_pauseButton->setText("Pause");
        m_pauseButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #5856D6;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 7px 14px;"
            "  font-weight: 600;"
            "  font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #6D6BE0;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #4A48C0;"
            "}");
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

    // Dark rounded rectangle background with subtle gradient border
    QRectF bgRect = QRectF(rect()).adjusted(1, 1, -1, -1);

    // Draw subtle glow/border
    QLinearGradient borderGradient(bgRect.topLeft(), bgRect.topRight());
    borderGradient.setColorAt(0.0, QColor(0, 122, 255, 80));
    borderGradient.setColorAt(0.5, QColor(175, 82, 222, 80));
    borderGradient.setColorAt(1.0, QColor(0, 122, 255, 80));
    painter.setPen(QPen(QBrush(borderGradient), 1.5));
    painter.setBrush(QColor(25, 25, 28, 245));
    painter.drawRoundedRect(bgRect, 12, 12);
}

void RecordingControlBar::positionNear(const QRect &recordingRegion)
{
    // Position below the recording region, centered
    int x = recordingRegion.center().x() - width() / 2;
    int y = recordingRegion.bottom() + 20;

    // Ensure within screen bounds
    QScreen *screen = QGuiApplication::screenAt(recordingRegion.center());
    if (screen) {
        QRect screenGeom = screen->geometry();

        // Horizontal bounds
        x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

        // Vertical bounds - if below would be off screen, position above
        if (y + height() > screenGeom.bottom() - 10) {
            y = recordingRegion.top() - height() - 20;
        }

        // If still off screen (above top), position inside the region
        if (y < screenGeom.top() + 10) {
            y = recordingRegion.top() + 10;
        }
    }

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
        // Check if click is not on a button
        QWidget *child = childAt(event->pos());
        if (!child || (!qobject_cast<QPushButton*>(child))) {
            m_isDragging = true;
            m_dragStartPos = event->globalPosition().toPoint();
            m_dragStartWidgetPos = pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }
    QWidget::mousePressEvent(event);
}

void RecordingControlBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        QPoint newPos = m_dragStartWidgetPos + delta;

        // Constrain to screen
        QScreen *screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
        if (screen) {
            QRect screenGeom = screen->geometry();
            newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - width()));
            newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - height()));
        }

        move(newPos);
    }
    QWidget::mouseMoveEvent(event);
}

void RecordingControlBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

void RecordingControlBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit stopRequested();
    } else {
        QWidget::keyPressEvent(event);
    }
}
