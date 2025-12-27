#include "pinwindow/ClickThroughExitButton.h"
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

    // Timer to periodically ensure button stays on top
    m_raiseTimer = new QTimer(this);
    m_raiseTimer->setInterval(100); // Check every 100ms
    connect(m_raiseTimer, &QTimer::timeout, this, &ClickThroughExitButton::ensureOnTop);
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

void ClickThroughExitButton::detach()
{
    if (m_targetWindow) {
        m_targetWindow->removeEventFilter(this);
        m_targetWindow = nullptr;
    }
    hide();
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
            raise();
        } else if (event->type() == QEvent::WindowActivate) {
            // When target window is activated, ensure exit button stays on top
            if (isVisible()) {
                raise();
            }
        }
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
    setCursor(Qt::PointingHandCursor);
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
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

void ClickThroughExitButton::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_raiseTimer->start();
    raise();
}

void ClickThroughExitButton::hideEvent(QHideEvent* event)
{
    m_raiseTimer->stop();
    QWidget::hideEvent(event);
}

void ClickThroughExitButton::ensureOnTop()
{
    if (isVisible() && m_targetWindow) {
        updatePosition();
        raise();
    }
}
