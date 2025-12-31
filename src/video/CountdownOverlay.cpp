#include "video/CountdownOverlay.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "platform/WindowLevel.h"

#include <QPainter>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

CountdownOverlay::CountdownOverlay(QWidget *parent)
    : QWidget(parent)
    , m_secondTimer(new QTimer(this))
    , m_scaleAnimation(new QPropertyAnimation(this, "animationProgress", this))
{
    setupWindow();

    // Configure second timer
    m_secondTimer->setSingleShot(true);
    connect(m_secondTimer, &QTimer::timeout, this, &CountdownOverlay::onSecondElapsed);

    // Configure scale animation (runs for each second)
    m_scaleAnimation->setDuration(800);  // Animation duration per second
    m_scaleAnimation->setStartValue(0.0);
    m_scaleAnimation->setEndValue(1.0);
    m_scaleAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_scaleAnimation, &QPropertyAnimation::valueChanged, this, [this]() {
        update();
    });
    connect(m_scaleAnimation, &QPropertyAnimation::finished,
            this, &CountdownOverlay::onAnimationFinished);
}

CountdownOverlay::~CountdownOverlay()
{
    if (m_secondTimer->isActive()) {
        m_secondTimer->stop();
    }
    if (m_scaleAnimation->state() == QAbstractAnimation::Running) {
        m_scaleAnimation->stop();
    }
}

void CountdownOverlay::setupWindow()
{
    setWindowFlags(Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint |
                   Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);

    // Ensure we can receive key events
    setFocusPolicy(Qt::StrongFocus);
}

void CountdownOverlay::setRegion(const QRect &region, QScreen *screen)
{
    m_region = region;
    m_screen = screen;
    setGeometry(region);
}

void CountdownOverlay::setCountdownSeconds(int seconds)
{
    m_totalSeconds = qBound(1, seconds, 10);
    m_currentSecond = m_totalSeconds;
}

void CountdownOverlay::start()
{
    if (m_running) {
        qDebug() << "CountdownOverlay: Already running";
        return;
    }

    qDebug() << "CountdownOverlay: Starting countdown from" << m_totalSeconds;
    m_running = true;
    m_currentSecond = m_totalSeconds;

    // Raise above menu bar on macOS
    raiseWindowAboveMenuBar(this);

    // Show and grab focus
    show();
    raise();
    activateWindow();
    setFocus();

    // Start the first second animation
    startNextSecond();
}

void CountdownOverlay::cancel()
{
    if (!m_running) {
        return;
    }

    qDebug() << "CountdownOverlay: Cancelled";
    m_running = false;

    m_secondTimer->stop();
    m_scaleAnimation->stop();

    emit countdownCancelled();
    close();
}

void CountdownOverlay::setAnimationProgress(qreal progress)
{
    m_animationProgress = progress;
    update();
}

void CountdownOverlay::startNextSecond()
{
    // Reset animation progress and start animation
    m_animationProgress = 0.0;
    m_scaleAnimation->start();

    // Schedule the next second
    m_secondTimer->start(1000);
}

void CountdownOverlay::onSecondElapsed()
{
    m_currentSecond--;

    if (m_currentSecond <= 0) {
        qDebug() << "CountdownOverlay: Countdown finished";
        m_running = false;
        emit countdownFinished();
        close();
    } else {
        startNextSecond();
    }
}

void CountdownOverlay::onAnimationFinished()
{
    // Animation for current number finished
    // The timer will handle transitioning to the next number
}

void CountdownOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // Draw semi-transparent dark overlay
    QColor overlayColor(0, 0, 0, 128);  // 50% opacity black
    painter.fillRect(rect(), overlayColor);

    // Draw the countdown number
    if (m_currentSecond > 0) {
        // Calculate scale based on animation progress
        // Start large (1.5x) and shrink to normal (1.0x)
        qreal scale = 1.5 - (0.5 * m_animationProgress);

        // Calculate opacity (fade in quickly, then stay)
        qreal opacity = qMin(1.0, m_animationProgress * 3.0);

        // Setup font
        QFont font = painter.font();
        font.setPixelSize(200);
        font.setWeight(QFont::Bold);
        painter.setFont(font);

        // Calculate text position (centered)
        QString text = QString::number(m_currentSecond);
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(text);
        QPointF center = QRectF(rect()).center();

        // Apply transformations
        painter.save();
        painter.translate(center);
        painter.scale(scale, scale);

        // Draw text with glow effect
        QColor textColor(255, 255, 255, int(255 * opacity));
        QColor glowColor(255, 255, 255, int(80 * opacity));

        // Draw glow (multiple passes with offset)
        painter.setPen(glowColor);
        for (int dx = -2; dx <= 2; dx++) {
            for (int dy = -2; dy <= 2; dy++) {
                if (dx != 0 || dy != 0) {
                    painter.drawText(QPointF(-textRect.width() / 2.0 + dx,
                                            textRect.height() / 4.0 + dy),
                                    text);
                }
            }
        }

        // Draw main text
        painter.setPen(textColor);
        painter.drawText(QPointF(-textRect.width() / 2.0,
                                textRect.height() / 4.0),
                        text);

        painter.restore();
    }
}

void CountdownOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancel();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
