#include "RecordingBoundaryOverlay.h"

#include <QPainter>
#include <QPen>
#include <QConicalGradient>
#include <QtMath>

RecordingBoundaryOverlay::RecordingBoundaryOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_animationTimer(nullptr)
    , m_gradientOffset(0.0)
    , m_borderMode(BorderMode::Recording)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);  // Click-through
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);

    // Animation timer for gradient rotation
    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, [this]() {
        m_gradientOffset += 0.005;
        if (m_gradientOffset > 1.0)
            m_gradientOffset -= 1.0;
        update();
    });
    m_animationTimer->start(16);  // ~60 FPS
}

RecordingBoundaryOverlay::~RecordingBoundaryOverlay()
{
    if (m_animationTimer) {
        m_animationTimer->stop();
    }
}

void RecordingBoundaryOverlay::setRegion(const QRect &region)
{
    m_region = region;

    // Set geometry to cover the region plus border
    setGeometry(region.adjusted(-BORDER_WIDTH, -BORDER_WIDTH,
                                BORDER_WIDTH, BORDER_WIDTH));
}

void RecordingBoundaryOverlay::setBorderMode(BorderMode mode)
{
    if (m_borderMode != mode) {
        m_borderMode = mode;
        update();
    }
}

void RecordingBoundaryOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF borderRect = QRectF(rect()).adjusted(BORDER_WIDTH / 2.0, BORDER_WIDTH / 2.0,
                                                 -BORDER_WIDTH / 2.0, -BORDER_WIDTH / 2.0);

    switch (m_borderMode) {
    case BorderMode::Recording:
        drawRecordingBorder(painter, borderRect.toRect());
        break;
    case BorderMode::Playing:
        drawPlayingBorder(painter, borderRect.toRect());
        break;
    case BorderMode::Paused:
        drawPausedBorder(painter, borderRect.toRect());
        break;
    }
}

void RecordingBoundaryOverlay::drawRecordingBorder(QPainter &painter, const QRect &borderRect)
{
    QRectF rectF(borderRect);
    QPointF center = rectF.center();

    // Draw subtle outer glow (no rounded corners for recording mode)
    for (int i = 3; i >= 1; --i) {
        QColor glowColor(88, 86, 214, 30 / i);  // Indigo with fading alpha
        int glowWidth = BORDER_WIDTH + i * 2;
        QPen glowPen(glowColor, glowWidth);
        glowPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(glowPen);
        painter.setBrush(Qt::NoBrush);
        QRectF glowRect = rectF.adjusted(-i, -i, i, i);
        painter.drawRect(glowRect);
    }

    // Create animated conical gradient for the border
    QConicalGradient gradient(center, 360 * m_gradientOffset);
    gradient.setColorAt(0.0, QColor(0, 122, 255));      // Apple Blue
    gradient.setColorAt(0.25, QColor(88, 86, 214));     // Indigo
    gradient.setColorAt(0.5, QColor(175, 82, 222));     // Purple
    gradient.setColorAt(0.75, QColor(191, 90, 242));    // Light Purple
    gradient.setColorAt(1.0, QColor(0, 122, 255));      // Back to Blue

    QPen pen(QBrush(gradient), BORDER_WIDTH);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rectF);
}

void RecordingBoundaryOverlay::drawPlayingBorder(QPainter &painter, const QRect &borderRect)
{
    QRectF rectF(borderRect);

    // Pulsing effect for playing state
    qreal pulseAlpha = 0.6 + 0.4 * qSin(m_gradientOffset * 2 * M_PI);
    int alpha = static_cast<int>(255 * pulseAlpha);

    // Draw subtle outer glow with green (no rounded corners)
    for (int i = 3; i >= 1; --i) {
        QColor glowColor(52, 199, 89, (30 / i) * pulseAlpha);  // Green with pulsing alpha
        int glowWidth = BORDER_WIDTH + i * 2;
        QPen glowPen(glowColor, glowWidth);
        glowPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(glowPen);
        painter.setBrush(Qt::NoBrush);
        QRectF glowRect = rectF.adjusted(-i, -i, i, i);
        painter.drawRect(glowRect);
    }

    // Green border with pulsing alpha (no rounded corners)
    QColor playingColor(52, 199, 89, alpha);  // Green
    QPen pen(playingColor, BORDER_WIDTH);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rectF);
}

void RecordingBoundaryOverlay::drawPausedBorder(QPainter &painter, const QRect &borderRect)
{
    QRectF rectF(borderRect);

    // Draw subtle outer glow with amber (no rounded corners)
    for (int i = 3; i >= 1; --i) {
        QColor glowColor(255, 184, 0, 30 / i);  // Amber with fading alpha
        int glowWidth = BORDER_WIDTH + i * 2;
        QPen glowPen(glowColor, glowWidth);
        glowPen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(glowPen);
        painter.setBrush(Qt::NoBrush);
        QRectF glowRect = rectF.adjusted(-i, -i, i, i);
        painter.drawRect(glowRect);
    }

    // Static amber border (no rounded corners)
    QColor pausedColor(255, 184, 0);  // Amber
    QPen pen(pausedColor, BORDER_WIDTH);
    pen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rectF);
}
