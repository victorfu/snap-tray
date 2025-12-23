#include "RecordingControlBar.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

RecordingControlBar::RecordingControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_pauseButton(nullptr)
    , m_isDragging(false)
    , m_indicatorVisible(true)
    , m_isPaused(false)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);  // Don't steal focus
    setFixedHeight(44);

    setupUi();

    // Recording indicator stays solid (no blinking)
    m_blinkTimer = nullptr;
}

RecordingControlBar::~RecordingControlBar()
{
}

void RecordingControlBar::setupUi()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(12);

    // Recording indicator (red circle)
    m_recordingIndicator = new QLabel(this);
    m_recordingIndicator->setFixedSize(14, 14);
    m_recordingIndicator->setStyleSheet(
        "background-color: #FF3B30; border-radius: 7px;");
    layout->addWidget(m_recordingIndicator);

    // Duration label
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setStyleSheet(
        "color: white; font-size: 15px; font-weight: bold; font-family: 'SF Mono', Menlo, Monaco, monospace;");
    m_durationLabel->setMinimumWidth(80);
    layout->addWidget(m_durationLabel);

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
        "  border-radius: 5px;"
        "  padding: 6px 18px;"
        "  font-weight: bold;"
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
        "  background-color: #555;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 5px;"
        "  padding: 6px 14px;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #777;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #444;"
        "}");
    connect(m_cancelButton, &QPushButton::clicked, this, &RecordingControlBar::cancelRequested);
    layout->addWidget(m_cancelButton);

    adjustSize();
}

void RecordingControlBar::setPaused(bool paused)
{
    m_isPaused = paused;
    updatePauseButton();

    // Update indicator appearance (yellow when paused, red when recording)
    if (paused) {
        m_recordingIndicator->setStyleSheet(
            "background-color: #FFB800; border-radius: 7px;");  // Yellow when paused
    } else {
        m_recordingIndicator->setStyleSheet(
            "background-color: #FF3B30; border-radius: 7px;");  // Red when recording
    }
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
            "  border-radius: 5px;"
            "  padding: 6px 14px;"
            "  font-weight: bold;"
            "  font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #30B350;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #2A9D48;"
            "}");
    } else {
        // Show "Pause" button (orange)
        m_pauseButton->setText("Pause");
        m_pauseButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #FF9500;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 5px;"
            "  padding: 6px 14px;"
            "  font-weight: bold;"
            "  font-size: 13px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #E68600;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #CC7700;"
            "}");
    }
}

void RecordingControlBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Dark rounded rectangle background
    QColor bgColor(30, 30, 30, 240);
    painter.setBrush(bgColor);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
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
