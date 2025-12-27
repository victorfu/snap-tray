#include "scrolling/ScrollingCaptureOverlay.h"
#include "region/SelectionStateManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QScreen>
#include <QGuiApplication>

const QColor ScrollingCaptureOverlay::BORDER_SELECTING = QColor(100, 100, 100, 200);
const QColor ScrollingCaptureOverlay::BORDER_ADJUSTING = QColor(255, 59, 48);      // Red (same as capturing)
const QColor ScrollingCaptureOverlay::BORDER_CAPTURING = QColor(255, 59, 48);      // Red
const QColor ScrollingCaptureOverlay::BORDER_MATCH_FAIL = QColor(255, 0, 0);       // Bright Red

ScrollingCaptureOverlay::ScrollingCaptureOverlay(QWidget *parent)
    : QWidget(parent)
    , m_selectionManager(new SelectionStateManager(this))
    , m_flickerTimer(new QTimer(this))
{
    // NOTE: Removed Qt::Tool flag as it causes the window to hide when app loses focus on macOS
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    connect(m_flickerTimer, &QTimer::timeout, this, &ScrollingCaptureOverlay::onFlickerTimer);
}

ScrollingCaptureOverlay::~ScrollingCaptureOverlay()
{
    m_flickerTimer->stop();
}

void ScrollingCaptureOverlay::initializeForScreen(QScreen *screen)
{
    m_screen = screen ? screen : QGuiApplication::primaryScreen();
    m_allowNewSelection = true;

    // Set geometry to cover the entire screen
    setGeometry(m_screen->geometry());

    m_selectionManager->setBounds(rect());

    show();
    raise();
    activateWindow();
    setFocus();
}

void ScrollingCaptureOverlay::initializeForScreenWithRegion(QScreen *screen, const QRect &globalRegion)
{
    m_screen = screen ? screen : QGuiApplication::primaryScreen();
    m_allowNewSelection = false;

    // Set geometry to cover the entire screen
    setGeometry(m_screen->geometry());

    m_selectionManager->setBounds(rect());

    // Convert global region to local coordinates and set selection
    QRect localRegion = globalRegion.translated(-m_screen->geometry().topLeft());
    m_selectionManager->setSelectionRect(localRegion);

    // Skip Selecting state - go directly to Adjusting
    m_borderState = BorderState::Adjusting;

    show();
    raise();
    activateWindow();
    setFocus();
    update();  // Trigger repaint to draw border
}

void ScrollingCaptureOverlay::setBorderState(BorderState state)
{
    if (m_borderState == state) {
        return;
    }

    // Stop any existing animation
    stopMatchFailedAnimation();

    m_borderState = state;

    if (state == BorderState::MatchFailed) {
        startMatchFailedAnimation();
    }

    update();
}

void ScrollingCaptureOverlay::setAllowNewSelection(bool allow)
{
    m_allowNewSelection = allow;
}

QRect ScrollingCaptureOverlay::captureRegion() const
{
    QRect region = m_selectionManager->selectionRect();
    if (m_screen) {
        // Convert to global coordinates
        region.translate(m_screen->geometry().topLeft());
    }
    return region;
}

void ScrollingCaptureOverlay::setRegionLocked(bool locked)
{
    m_regionLocked = locked;
    setAttribute(Qt::WA_TransparentForMouseEvents, locked);
    update();
}

void ScrollingCaptureOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw semi-transparent overlay
    drawOverlay(painter);

    // Draw selection border and handles
    if (m_selectionManager->hasSelection()) {
        drawBorder(painter);

        if (!m_regionLocked && m_borderState == BorderState::Adjusting) {
            drawResizeHandles(painter);
        }

        drawDimensions(painter);
    }

    // Draw instructions when selecting
    if (m_borderState == BorderState::Selecting && !m_selectionManager->hasSelection()) {
        drawInstructions(painter);
    }
}

void ScrollingCaptureOverlay::drawOverlay(QPainter &painter)
{
    QRect selRect = m_selectionManager->selectionRect();

    // Draw dark overlay outside selection
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 120));

    if (selRect.isValid() && !selRect.isEmpty()) {
        // Top region
        painter.drawRect(0, 0, width(), selRect.top());
        // Bottom region
        painter.drawRect(0, selRect.bottom() + 1, width(), height() - selRect.bottom() - 1);
        // Left region
        painter.drawRect(0, selRect.top(), selRect.left(), selRect.height());
        // Right region
        painter.drawRect(selRect.right() + 1, selRect.top(), width() - selRect.right() - 1, selRect.height());
    } else {
        // No selection - fill entire area
        painter.drawRect(rect());
    }
}

void ScrollingCaptureOverlay::drawBorder(QPainter &painter)
{
    QRect selRect = m_selectionManager->selectionRect();
    if (!selRect.isValid()) {
        return;
    }

    QColor borderColor;
    int borderWidth = BORDER_WIDTH;

    switch (m_borderState) {
    case BorderState::Selecting:
        borderColor = BORDER_SELECTING;
        borderWidth = 1;
        break;
    case BorderState::Adjusting:
        borderColor = BORDER_ADJUSTING;
        break;
    case BorderState::Capturing:
        borderColor = BORDER_CAPTURING;
        break;
    case BorderState::MatchFailed:
        borderColor = m_flickerState ? BORDER_MATCH_FAIL : BORDER_CAPTURING;
        borderWidth = m_flickerState ? 5 : BORDER_WIDTH;
        break;
    }

    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(selRect.adjusted(-borderWidth/2, -borderWidth/2, borderWidth/2, borderWidth/2));
}

void ScrollingCaptureOverlay::drawResizeHandles(QPainter &painter)
{
    QRect selRect = m_selectionManager->selectionRect();
    if (!selRect.isValid()) {
        return;
    }

    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(BORDER_ADJUSTING);

    QList<QPoint> handlePositions = {
        selRect.topLeft(),
        QPoint(selRect.center().x(), selRect.top()),
        selRect.topRight(),
        QPoint(selRect.left(), selRect.center().y()),
        QPoint(selRect.right(), selRect.center().y()),
        selRect.bottomLeft(),
        QPoint(selRect.center().x(), selRect.bottom()),
        selRect.bottomRight()
    };

    for (const QPoint &pos : handlePositions) {
        QRect handleRect(pos.x() - HANDLE_SIZE/2, pos.y() - HANDLE_SIZE/2,
                         HANDLE_SIZE, HANDLE_SIZE);
        painter.drawRect(handleRect);
    }
}

void ScrollingCaptureOverlay::drawDimensions(QPainter &painter)
{
    QRect selRect = m_selectionManager->selectionRect();
    if (!selRect.isValid()) {
        return;
    }

    QString dimensions = QString("%1 × %2").arg(selRect.width()).arg(selRect.height());

    painter.setFont(QFont("Arial", 11));
    QFontMetrics fm(painter.font());
    QRect textRect = fm.boundingRect(dimensions);

    int x = selRect.left();
    int y = selRect.bottom() + 20;

    // If text would go off screen, put it above
    if (y + textRect.height() > height() - 10) {
        y = selRect.top() - 10;
    }

    // Background for text
    QRect bgRect(x - 4, y - textRect.height(), textRect.width() + 8, textRect.height() + 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(bgRect, 4, 4);

    // Text
    painter.setPen(Qt::white);
    painter.drawText(x, y, dimensions);
}

void ScrollingCaptureOverlay::drawInstructions(QPainter &painter)
{
    QString instructions = "Drag to select capture region\nPress Esc to cancel";

    painter.setFont(QFont("Arial", 14));
    painter.setPen(Qt::white);

    QRect textRect = painter.fontMetrics().boundingRect(rect(), Qt::AlignCenter | Qt::TextWordWrap, instructions);
    textRect.adjust(-20, -10, 20, 10);

    // Background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(textRect, 8, 8);

    // Text
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, instructions);
}

void ScrollingCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (m_regionLocked) {
        return;
    }

    QPoint pos = event->pos();

    if (m_borderState == BorderState::Adjusting) {
        // Check for handle hit
        SelectionStateManager::ResizeHandle handle = m_selectionManager->hitTestHandle(pos);
        if (handle != SelectionStateManager::ResizeHandle::None) {
            m_selectionManager->startResize(pos, handle);
            return;
        }

        // Check for move hit
        if (m_selectionManager->selectionRect().contains(pos)) {
            m_selectionManager->startMove(pos);
            return;
        }
    }

    if (!m_allowNewSelection && m_selectionManager->hasSelection()) {
        return;
    }

    // Start new selection
    m_selectionManager->startSelection(pos);
    m_borderState = BorderState::Selecting;
    update();
}

void ScrollingCaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_regionLocked) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QPoint pos = event->pos();

    switch (m_selectionManager->state()) {
    case SelectionStateManager::State::Selecting:
        m_selectionManager->updateSelection(pos);
        break;
    case SelectionStateManager::State::Moving:
        m_selectionManager->updateMove(pos);
        break;
    case SelectionStateManager::State::ResizingHandle:
        m_selectionManager->updateResize(pos);
        break;
    default:
        // Update cursor for handle hover
        if (m_borderState == BorderState::Adjusting) {
            SelectionStateManager::ResizeHandle handle = m_selectionManager->hitTestHandle(pos);
            if (handle != SelectionStateManager::ResizeHandle::None) {
                // Set appropriate cursor based on handle
                switch (handle) {
                case SelectionStateManager::ResizeHandle::TopLeft:
                case SelectionStateManager::ResizeHandle::BottomRight:
                    setCursor(Qt::SizeFDiagCursor);
                    break;
                case SelectionStateManager::ResizeHandle::TopRight:
                case SelectionStateManager::ResizeHandle::BottomLeft:
                    setCursor(Qt::SizeBDiagCursor);
                    break;
                case SelectionStateManager::ResizeHandle::Top:
                case SelectionStateManager::ResizeHandle::Bottom:
                    setCursor(Qt::SizeVerCursor);
                    break;
                case SelectionStateManager::ResizeHandle::Left:
                case SelectionStateManager::ResizeHandle::Right:
                    setCursor(Qt::SizeHorCursor);
                    break;
                default:
                    break;
                }
            } else if (m_selectionManager->selectionRect().contains(pos)) {
                setCursor(Qt::SizeAllCursor);
            } else {
                setCursor(Qt::CrossCursor);
            }
        }
        break;
    }

    update();
}

void ScrollingCaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    if (m_regionLocked) {
        return;
    }

    switch (m_selectionManager->state()) {
    case SelectionStateManager::State::Selecting:
        m_selectionManager->finishSelection();
        if (m_selectionManager->hasSelection()) {
            m_borderState = BorderState::Adjusting;
            emit regionSelected(captureRegion());
        }
        break;
    case SelectionStateManager::State::Moving:
        m_selectionManager->finishMove();
        emit regionChanged(captureRegion());
        break;
    case SelectionStateManager::State::ResizingHandle:
        m_selectionManager->finishResize();
        emit regionChanged(captureRegion());
        break;
    default:
        break;
    }

    update();
}

void ScrollingCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        emit cancelled();
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (m_selectionManager->hasSelection()) {
            if (m_borderState == BorderState::Adjusting) {
                emit selectionConfirmed();
            } else if (m_borderState == BorderState::Capturing || m_borderState == BorderState::MatchFailed) {
                emit stopRequested();
            }
        }
        break;

    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (!m_regionLocked && m_selectionManager->hasSelection()) {
            int delta = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
            QRect selRect = m_selectionManager->selectionRect();

            switch (event->key()) {
            case Qt::Key_Up:    selRect.translate(0, -delta); break;
            case Qt::Key_Down:  selRect.translate(0, delta); break;
            case Qt::Key_Left:  selRect.translate(-delta, 0); break;
            case Qt::Key_Right: selRect.translate(delta, 0); break;
            }

            m_selectionManager->setSelectionRect(selRect);
            emit regionChanged(captureRegion());
            update();
        }
        break;

    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void ScrollingCaptureOverlay::startMatchFailedAnimation()
{
    m_flickerCount = 0;
    m_flickerState = true;
    m_flickerTimer->start(FLICKER_INTERVAL_MS);
}

void ScrollingCaptureOverlay::stopMatchFailedAnimation()
{
    m_flickerTimer->stop();
    m_flickerState = false;
    m_flickerCount = 0;
}

void ScrollingCaptureOverlay::onFlickerTimer()
{
    m_flickerState = !m_flickerState;
    m_flickerCount++;

    if (m_flickerCount >= FLICKER_MAX_COUNT) {
        stopMatchFailedAnimation();
    }

    update();
}

void ScrollingCaptureOverlay::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Ensure we have focus when shown
    raise();
    activateWindow();
    setFocus();
}

bool ScrollingCaptureOverlay::event(QEvent *event)
{
    // Regain focus when window becomes active
    if (event->type() == QEvent::WindowActivate) {
        setFocus();
    }
    return QWidget::event(event);
}
