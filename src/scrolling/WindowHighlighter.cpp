#include "scrolling/WindowHighlighter.h"
#include "WindowDetector.h"
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>

WindowHighlighter::WindowHighlighter(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
#ifdef Q_OS_MAC
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Update timer for cursor tracking
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &WindowHighlighter::updateHighlight);

    // Animation timer for capturing mode border
    m_animationTimer = new QTimer(this);
    connect(m_animationTimer, &QTimer::timeout, this, &WindowHighlighter::updateAnimation);
}

WindowHighlighter::~WindowHighlighter()
{
    stopTracking();
}

void WindowHighlighter::setWindowDetector(WindowDetector* detector)
{
    m_detector = detector;
}

void WindowHighlighter::startTracking()
{
    if (m_isTracking) return;
    m_isTracking = true;

    // Cover the screen where the cursor is located
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        setGeometry(screen->geometry());
        m_currentScreen = screen;
    }

    // Note: Window list refresh is handled by ScrollingCaptureManager
    // to avoid duplicate calls that cause stale results

    m_updateTimer->start(kUpdateIntervalMs);
    show();
    raise();
    activateWindow();
    setFocus();
}

void WindowHighlighter::stopTracking()
{
    m_isTracking = false;
    m_updateTimer->stop();
    m_animationTimer->stop();
    m_highlightRect = QRect();
    m_highlightedWindow = 0;
    m_controlBarRect = QRect();
    m_startButtonRect = QRect();
    m_cancelButtonRect = QRect();
    m_currentScreen = nullptr;
    hide();
}

void WindowHighlighter::setCapturing(bool capturing)
{
    m_isCapturing = capturing;
    if (capturing) {
        m_animationTimer->start(kUpdateIntervalMs);
        // Make overlay transparent to mouse events so scroll can pass through
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    } else {
        m_animationTimer->stop();
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    update();
}

void WindowHighlighter::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus();
}

void WindowHighlighter::updateHighlight()
{
    if (!m_isTracking) return;

    QPoint cursorPos = QCursor::pos();

    QScreen* screen = QGuiApplication::screenAt(cursorPos);
    if (screen && screen != m_currentScreen) {
        m_currentScreen = screen;
        setGeometry(screen->geometry());
        updateButtonRects();
        update();
        if (m_detector) {
            m_detector->setScreen(screen);
            m_detector->refreshWindowListAsync();
        }
    }

    // Check if cursor is over the control bar - don't change highlight
    if (!m_controlBarRect.isNull() && m_controlBarRect.contains(cursorPos)) {
        return;
    }

    // Detect window under cursor
    if (m_detector) {
        auto element = m_detector->detectWindowAt(cursorPos);
        if (element.has_value()) {
            QRect newRect = element->bounds;
            WId newWindow = element->windowId;

            if (newRect != m_highlightRect || newWindow != m_highlightedWindow) {
                m_highlightRect = newRect;
                m_highlightedWindow = newWindow;
                m_windowTitle = element->windowTitle;
                updateButtonRects();
                emit windowHovered(m_highlightedWindow, m_windowTitle);
                update();
            }
        } else {
            // Cursor not over any window - keep current highlight
            // This allows user to move to the control bar
        }
    }
}

void WindowHighlighter::updateAnimation()
{
    m_animationOffset += 0.5;
    if (m_animationOffset > kDashLength * 2) {
        m_animationOffset = 0;
    }
    update();
}

void WindowHighlighter::updateButtonRects()
{
    if (m_highlightRect.isNull()) {
        m_controlBarRect = QRect();
        m_startButtonRect = QRect();
        m_cancelButtonRect = QRect();
        return;
    }

    // Position control bar at the bottom center of the highlighted window
    int barWidth = kButtonWidth * 2 + kButtonSpacing + kControlBarPadding * 2;
    int barX = m_highlightRect.center().x() - barWidth / 2;
    int barY = m_highlightRect.bottom() - kControlBarHeight - 10;

    // Keep bar within highlight rect
    barX = qMax(m_highlightRect.left() + 10, barX);
    barX = qMin(m_highlightRect.right() - barWidth - 10, barX);
    barY = qMax(m_highlightRect.top() + 10, barY);

    m_controlBarRect = QRect(barX, barY, barWidth, kControlBarHeight);

    // Button positions within the control bar
    int buttonY = barY + (kControlBarHeight - kButtonHeight) / 2;
    int startX = barX + kControlBarPadding;
    int cancelX = startX + kButtonWidth + kButtonSpacing;

    m_startButtonRect = QRect(startX, buttonY, kButtonWidth, kButtonHeight);
    m_cancelButtonRect = QRect(cancelX, buttonY, kButtonWidth, kButtonHeight);
}

int WindowHighlighter::buttonAtPosition(const QPoint& pos) const
{
    if (mapToLocal(m_startButtonRect).contains(pos)) return kButtonStart;
    if (mapToLocal(m_cancelButtonRect).contains(pos)) return kButtonCancel;
    return kButtonNone;
}

QRect WindowHighlighter::mapToLocal(const QRect& rect) const
{
    return rect.translated(-geometry().topLeft());
}

void WindowHighlighter::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw dimming overlay
    drawDimOverlay(painter);

    // Draw highlight border
    if (!m_highlightRect.isNull()) {
        if (m_isCapturing) {
            drawAnimatedBorder(painter);
        } else {
            drawHighlightBorder(painter);
            drawControlBar(painter);
        }
    }
}

void WindowHighlighter::drawDimOverlay(QPainter& painter)
{
    // Skip dim overlay during capture mode - only show the animated border
    if (m_isCapturing) {
        return;
    }

    // Semi-transparent overlay with cutout for highlighted window
    QColor dimColor(0, 0, 0, 80);

    QRect localHighlight = mapToLocal(m_highlightRect);

    if (localHighlight.isNull()) {
        painter.fillRect(rect(), dimColor);
    } else {
        // Use path subtraction for clean cutout
        QPainterPath dimPath;
        dimPath.addRect(rect());

        QPainterPath windowPath;
        windowPath.addRect(localHighlight);

        QPainterPath resultPath = dimPath.subtracted(windowPath);
        painter.fillPath(resultPath, dimColor);
    }
}

void WindowHighlighter::drawHighlightBorder(QPainter& painter)
{
    QRect localHighlight = mapToLocal(m_highlightRect);

    // Dashed border for selection mode
    QPen pen(QColor(0, 122, 255));  // macOS blue
    pen.setWidth(kBorderWidth);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({kDashLength / 2.0, kDashLength / 2.0});

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(localHighlight.adjusted(
        kBorderWidth / 2, kBorderWidth / 2,
        -kBorderWidth / 2, -kBorderWidth / 2));
}

void WindowHighlighter::drawAnimatedBorder(QPainter& painter)
{
    QRect localHighlight = mapToLocal(m_highlightRect);

    // Animated marching ants border for capturing mode
    QPen pen(QColor(255, 59, 48));  // Red for capturing
    pen.setWidth(kBorderWidth);
    pen.setStyle(Qt::DashLine);
    pen.setDashPattern({kDashLength / 2.0, kDashLength / 2.0});
    pen.setDashOffset(m_animationOffset);

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(localHighlight.adjusted(
        kBorderWidth / 2, kBorderWidth / 2,
        -kBorderWidth / 2, -kBorderWidth / 2));
}

void WindowHighlighter::drawControlBar(QPainter& painter)
{
    QRect localControlBar = mapToLocal(m_controlBarRect);
    QRect localStartButton = mapToLocal(m_startButtonRect);
    QRect localCancelButton = mapToLocal(m_cancelButtonRect);

    if (localControlBar.isNull()) return;

    // Draw control bar background (glass-like)
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(localControlBar, 8, 8);

    // Draw border
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(localControlBar, 8, 8);

    // Draw buttons
    drawButton(painter, localStartButton, tr("Start"), true, m_hoveredButton == kButtonStart);
    drawButton(painter, localCancelButton, tr("Cancel"), false, m_hoveredButton == kButtonCancel);
}

void WindowHighlighter::drawButton(QPainter& painter, const QRect& rect,
                                    const QString& text, bool isPrimary, bool isHovered)
{
    // Button background
    QColor bgColor;
    if (isPrimary) {
        bgColor = isHovered ? QColor(0, 122, 255) : QColor(0, 102, 204);
    } else {
        bgColor = isHovered ? QColor(80, 80, 80) : QColor(60, 60, 60);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(rect, 6, 6);

    // Button text
    painter.setPen(Qt::white);
    QFont buttonFont = painter.font();
    buttonFont.setPointSize(12);
    painter.setFont(buttonFont);
    painter.drawText(rect, Qt::AlignCenter, text);
}

void WindowHighlighter::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();

    // Update button hover state
    int newHovered = buttonAtPosition(pos);
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        setCursor(newHovered != kButtonNone ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void WindowHighlighter::mousePressEvent(QMouseEvent *event)
{
    // Button clicks are handled in mouseReleaseEvent for proper UX
    Q_UNUSED(event);
}

void WindowHighlighter::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        if (event->button() == Qt::RightButton) {
            emit cancelled();
        }
        return;
    }

    QPoint pos = event->pos();
    int button = buttonAtPosition(pos);

    if (button == kButtonStart) {
        if (m_highlightedWindow != 0 && !m_highlightRect.isNull()) {
            emit startClicked();
        }
    } else if (button == kButtonCancel) {
        emit cancelled();
    }
    // Clicks outside buttons do nothing (allow user to reposition cursor)
}

void WindowHighlighter::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_highlightedWindow != 0 && !m_highlightRect.isNull()) {
            emit startClicked();
        }
    }
}
