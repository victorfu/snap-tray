#include "RecordingBoundaryOverlay.h"

#include <QPainter>
#include <QPen>
#include <QConicalGradient>

RecordingBoundaryOverlay::RecordingBoundaryOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_animationTimer(nullptr)
    , m_gradientOffset(0.0)
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

void RecordingBoundaryOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF borderRect = QRectF(rect()).adjusted(BORDER_WIDTH / 2.0, BORDER_WIDTH / 2.0,
                                                 -BORDER_WIDTH / 2.0, -BORDER_WIDTH / 2.0);
    QPointF center = borderRect.center();

    // Draw subtle outer glow
    for (int i = 3; i >= 1; --i) {
        QColor glowColor(88, 86, 214, 30 / i);  // Indigo with fading alpha
        int glowWidth = BORDER_WIDTH + i * 2;
        QPen glowPen(glowColor, glowWidth);
        glowPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(glowPen);
        painter.setBrush(Qt::NoBrush);
        // Offset glow outward so its inner edge aligns with the recording region
        // This prevents glow from being captured in the recording
        QRectF glowRect = borderRect.adjusted(-i, -i, i, i);
        painter.drawRoundedRect(glowRect, CORNER_RADIUS, CORNER_RADIUS);
    }

    // Create animated conical gradient for the border
    QConicalGradient gradient(center, 360 * m_gradientOffset);
    gradient.setColorAt(0.0, QColor(0, 122, 255));      // Apple Blue
    gradient.setColorAt(0.25, QColor(88, 86, 214));     // Indigo
    gradient.setColorAt(0.5, QColor(175, 82, 222));     // Purple
    gradient.setColorAt(0.75, QColor(191, 90, 242));    // Light Purple
    gradient.setColorAt(1.0, QColor(0, 122, 255));      // Back to Blue

    QPen pen(QBrush(gradient), BORDER_WIDTH);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(borderRect, CORNER_RADIUS, CORNER_RADIUS);
}
