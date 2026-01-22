#include "pinwindow/ClickThroughExitButton.h"
#include "platform/WindowLevel.h"
#include "cursor/CursorManager.h"
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QHideEvent>

ClickThroughExitButton::ClickThroughExitButton(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    setupUI();

    // Timer to periodically update position (without raising/focus stealing)
    m_raiseTimer = new QTimer(this);
    m_raiseTimer->setInterval(100);
    connect(m_raiseTimer, &QTimer::timeout, this, &ClickThroughExitButton::updatePositionIfNeeded);
}

void ClickThroughExitButton::setupUI()
{
    m_label = new QLabel(this);
    m_label->setText("Click-through  \u2715");
    m_label->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(88, 86, 214, 200);"
        "  color: white;"
        "  padding: 4px 10px;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    );
    m_label->adjustSize();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_label);

    adjustSize();
}

void ClickThroughExitButton::attachTo(QWidget* targetWindow)
{
    if (m_targetWindow) {
        m_targetWindow->removeEventFilter(this);
    }

    m_targetWindow = targetWindow;

    if (m_targetWindow) {
        m_targetWindow->installEventFilter(this);
        updatePosition();
    }
}

void ClickThroughExitButton::updatePosition()
{
    if (!m_targetWindow)
        return;

    QPoint targetPos = m_targetWindow->pos();
    int targetWidth = m_targetWindow->width();

    // Position at top-right corner of target window
    int x = targetPos.x() + targetWidth - m_shadowMargin - width() - 8;
    int y = targetPos.y() + m_shadowMargin + 8;

    move(x, y);
}

bool ClickThroughExitButton::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_targetWindow) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            updatePosition();
        } else if (event->type() == QEvent::Close || event->type() == QEvent::Hide) {
            hide();
        } else if (event->type() == QEvent::Show && isVisible()) {
            updatePosition();
        }
        // Note: We intentionally do NOT call raise() here anymore as it steals focus.
        // The native window level set by setWindowFloatingWithoutFocus() handles this.
    }
    return QWidget::eventFilter(watched, event);
}

void ClickThroughExitButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit exitClicked();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void ClickThroughExitButton::enterEvent(QEnterEvent* event)
{
    m_label->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(108, 106, 234, 220);"
        "  color: white;"
        "  padding: 4px 10px;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    );
    CursorManager::instance().pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
    QWidget::enterEvent(event);
}

void ClickThroughExitButton::leaveEvent(QEvent* event)
{
    m_label->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(88, 86, 214, 200);"
        "  color: white;"
        "  padding: 4px 10px;"
        "  border-radius: 4px;"
        "  font-size: 11px;"
        "  font-weight: bold;"
        "}"
    );
    CursorManager::instance().clearAllForWidget(this);
    QWidget::leaveEvent(event);
}

void ClickThroughExitButton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Set up native floating window that doesn't steal focus
    // Must be called after the window is shown (has a valid winId)
    setWindowFloatingWithoutFocus(this);

    m_raiseTimer->start();
}

void ClickThroughExitButton::hideEvent(QHideEvent* event)
{
    m_raiseTimer->stop();
    QWidget::hideEvent(event);
}

void ClickThroughExitButton::updatePositionIfNeeded()
{
    if (isVisible() && m_targetWindow) {
        updatePosition();
        // Note: We do NOT call raise() here as it causes focus stealing.
        // The window level is managed by setWindowFloatingWithoutFocus().
    }
}
