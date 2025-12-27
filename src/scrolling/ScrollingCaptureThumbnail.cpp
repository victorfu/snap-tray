#include "scrolling/ScrollingCaptureThumbnail.h"

#include <QPainter>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>

ScrollingCaptureThumbnail::ScrollingCaptureThumbnail(QWidget *parent)
    : QWidget(parent)
{
    // NOTE: Removed Qt::Tool flag as it causes the window to hide when app loses focus on macOS
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setFixedWidth(THUMBNAIL_WIDTH_VERTICAL + 2 * MARGIN);
    setMinimumHeight(100);
}

ScrollingCaptureThumbnail::~ScrollingCaptureThumbnail()
{
}

void ScrollingCaptureThumbnail::setStitchedImage(const QImage &image)
{
    m_stitchedImage = image;
    updateScaledImage();
    update();
}

void ScrollingCaptureThumbnail::setViewportRect(const QRect &viewportInStitched)
{
    m_viewportRect = viewportInStitched;
    update();
}

void ScrollingCaptureThumbnail::setMatchStatus(MatchStatus status, double confidence)
{
    m_matchStatus = status;
    m_confidence = confidence;
    update();
}

void ScrollingCaptureThumbnail::setDirection(Direction direction)
{
    if (m_direction == direction) {
        return;
    }

    m_direction = direction;

    // Update size constraints based on direction
    int thumbnailWidth = (direction == Direction::Vertical)
        ? THUMBNAIL_WIDTH_VERTICAL : THUMBNAIL_WIDTH_HORIZONTAL;
    setFixedWidth(thumbnailWidth + 2 * MARGIN);

    updateScaledImage();
    update();
}

void ScrollingCaptureThumbnail::setLastSuccessfulPosition(int position)
{
    m_lastSuccessfulPosition = position;
    update();
}

void ScrollingCaptureThumbnail::setShowRecoveryHint(bool show)
{
    m_showRecoveryHint = show;
    update();
}

void ScrollingCaptureThumbnail::positionNear(const QRect &captureRegion)
{
    QScreen *screen = QGuiApplication::screenAt(captureRegion.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();

    // Prefer right side of capture region
    int x = captureRegion.right() + 16;
    int y = captureRegion.top();

    // If would go off right edge, try left side
    if (x + width() > screenGeom.right() - 10) {
        x = captureRegion.left() - width() - 16;
    }

    // If would go off left edge, position inside at right
    if (x < screenGeom.left() + 10) {
        x = captureRegion.right() - width() - 10;
    }

    // Vertical bounds
    y = qBound(screenGeom.top() + 10, y, screenGeom.bottom() - height() - 10);

    move(x, y);
}

void ScrollingCaptureThumbnail::clear()
{
    m_stitchedImage = QImage();
    m_scaledImage = QImage();
    m_viewportRect = QRect();
    m_matchStatus = MatchStatus::Good;
    m_confidence = 1.0;
    m_lastSuccessfulPosition = 0;
    m_showRecoveryHint = false;
    update();
}

void ScrollingCaptureThumbnail::updateScaledImage()
{
    if (m_stitchedImage.isNull()) {
        m_scaledImage = QImage();
        setFixedHeight(100);
        return;
    }

    // Get direction-aware size constraints
    int thumbnailWidth = (m_direction == Direction::Vertical)
        ? THUMBNAIL_WIDTH_VERTICAL : THUMBNAIL_WIDTH_HORIZONTAL;
    int thumbnailMaxHeight = (m_direction == Direction::Vertical)
        ? THUMBNAIL_MAX_HEIGHT_VERTICAL : THUMBNAIL_MAX_HEIGHT_HORIZONTAL;

    // Calculate scale to fit width
    m_scale = static_cast<double>(thumbnailWidth) / m_stitchedImage.width();

    int scaledHeight = static_cast<int>(m_stitchedImage.height() * m_scale);

    // Limit height - recalculate scale if needed
    if (scaledHeight > thumbnailMaxHeight) {
        scaledHeight = thumbnailMaxHeight;
        // Recalculate scale based on height constraint
        m_scale = static_cast<double>(thumbnailMaxHeight) / m_stitchedImage.height();
    }

    m_scaledImage = m_stitchedImage.scaled(
        thumbnailWidth, scaledHeight,
        Qt::KeepAspectRatio, Qt::SmoothTransformation);

    setFixedHeight(scaledHeight + 2 * MARGIN);
}

void ScrollingCaptureThumbnail::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background
    QRect bgRect = rect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40, 230));
    painter.drawRoundedRect(bgRect, BORDER_RADIUS, BORDER_RADIUS);

    // Draw border
    drawBorder(painter);

    // Draw scaled image
    if (!m_scaledImage.isNull()) {
        QRect imageRect(MARGIN, MARGIN, m_scaledImage.width(), m_scaledImage.height());
        painter.drawImage(imageRect, m_scaledImage);

        // Draw viewport indicator
        drawViewportIndicator(painter);

        // Draw recovery hint if match failed
        if (m_showRecoveryHint) {
            drawRecoveryHint(painter);
        }
    }

    // Draw match status indicator
    drawMatchStatusIndicator(painter);
}

void ScrollingCaptureThumbnail::drawBorder(QPainter &painter)
{
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), BORDER_RADIUS, BORDER_RADIUS);
}

void ScrollingCaptureThumbnail::drawViewportIndicator(QPainter &painter)
{
    if (m_viewportRect.isNull() || m_stitchedImage.isNull()) {
        return;
    }

    // Scale viewport rect to thumbnail coordinates
    QRectF scaledViewport(
        MARGIN + m_viewportRect.x() * m_scale,
        MARGIN + m_viewportRect.y() * m_scale,
        m_viewportRect.width() * m_scale,
        m_viewportRect.height() * m_scale
    );

    // Clip to image bounds
    QRect imageRect(MARGIN, MARGIN, m_scaledImage.width(), m_scaledImage.height());
    scaledViewport = scaledViewport.intersected(QRectF(imageRect));

    if (scaledViewport.isEmpty()) {
        return;
    }

    // Draw semi-transparent overlay
    painter.setPen(QPen(QColor(0, 122, 255), 2));
    painter.setBrush(QColor(0, 122, 255, 50));
    painter.drawRect(scaledViewport);
}

void ScrollingCaptureThumbnail::drawMatchStatusIndicator(QPainter &painter)
{
    // Draw status indicator in top-right corner
    int x = width() - STATUS_INDICATOR_SIZE - MARGIN;
    int y = MARGIN;

    QColor color;
    switch (m_matchStatus) {
    case MatchStatus::Good:
        color = QColor(76, 175, 80);   // Green
        break;
    case MatchStatus::Warning:
        color = QColor(255, 193, 7);   // Yellow
        break;
    case MatchStatus::Failed:
        color = QColor(255, 59, 48);   // Red
        break;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(x, y, STATUS_INDICATOR_SIZE, STATUS_INDICATOR_SIZE);

    // Draw confidence text below if not perfect
    if (m_confidence < 1.0 && m_confidence > 0.0) {
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 8));
        QString confText = QString("%1%").arg(static_cast<int>(m_confidence * 100));
        painter.drawText(x - 10, y + STATUS_INDICATOR_SIZE + 12, confText);
    }
}

void ScrollingCaptureThumbnail::drawRecoveryHint(QPainter &painter)
{
    if (m_stitchedImage.isNull() || m_lastSuccessfulPosition <= 0) {
        return;
    }

    // Draw a green line at the last successful position
    if (m_direction == Direction::Vertical) {
        // Vertical mode: draw horizontal green line
        int scaledY = MARGIN + static_cast<int>(m_lastSuccessfulPosition * m_scale);

        // Clamp to visible area
        int maxY = MARGIN + m_scaledImage.height();
        if (scaledY > maxY) scaledY = maxY;
        if (scaledY < MARGIN) scaledY = MARGIN;

        painter.setPen(QPen(QColor(76, 175, 80), 2));
        painter.drawLine(MARGIN, scaledY, MARGIN + m_scaledImage.width(), scaledY);

        // Draw arrow and text hint
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 9, QFont::Bold));
        QString hint = QString::fromUtf8("↑ Scroll here");
        QFontMetrics fm(painter.font());
        int textWidth = fm.horizontalAdvance(hint);
        int textX = MARGIN + (m_scaledImage.width() - textWidth) / 2;
        int textY = scaledY - 6;

        // Background for text
        QRect textBgRect(textX - 4, textY - fm.ascent() - 2, textWidth + 8, fm.height() + 4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(textBgRect, 4, 4);

        painter.setPen(QColor(76, 175, 80));
        painter.drawText(textX, textY, hint);
    } else {
        // Horizontal mode: draw vertical green line
        int scaledX = MARGIN + static_cast<int>(m_lastSuccessfulPosition * m_scale);

        // Clamp to visible area
        int maxX = MARGIN + m_scaledImage.width();
        if (scaledX > maxX) scaledX = maxX;
        if (scaledX < MARGIN) scaledX = MARGIN;

        painter.setPen(QPen(QColor(76, 175, 80), 2));
        painter.drawLine(scaledX, MARGIN, scaledX, MARGIN + m_scaledImage.height());

        // Draw arrow and text hint
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 9, QFont::Bold));
        QString hint = QString::fromUtf8("← Scroll");
        QFontMetrics fm(painter.font());
        int textHeight = fm.height();
        int textX = scaledX - fm.horizontalAdvance(hint) - 6;
        int textY = MARGIN + m_scaledImage.height() / 2 + textHeight / 4;

        // Background for text
        QRect textBgRect(textX - 4, textY - fm.ascent() - 2, fm.horizontalAdvance(hint) + 8, fm.height() + 4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(textBgRect, 4, 4);

        painter.setPen(QColor(76, 175, 80));
        painter.drawText(textX, textY, hint);
    }
}

void ScrollingCaptureThumbnail::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
    }
    QWidget::mousePressEvent(event);
}

void ScrollingCaptureThumbnail::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        move(m_dragStartWidgetPos + delta);
    }
    QWidget::mouseMoveEvent(event);
}

void ScrollingCaptureThumbnail::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}
