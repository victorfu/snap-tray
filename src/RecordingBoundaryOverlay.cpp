#include "RecordingBoundaryOverlay.h"

#include <QPainter>
#include <QPen>

RecordingBoundaryOverlay::RecordingBoundaryOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);  // Click-through
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
}

RecordingBoundaryOverlay::~RecordingBoundaryOverlay()
{
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

    // Draw red border
    QPen pen(QColor(255, 59, 48), BORDER_WIDTH);  // iOS-style red
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Draw just inside the widget bounds
    QRect borderRect = rect().adjusted(BORDER_WIDTH / 2, BORDER_WIDTH / 2,
                                       -BORDER_WIDTH / 2, -BORDER_WIDTH / 2);
    painter.drawRect(borderRect);
}
