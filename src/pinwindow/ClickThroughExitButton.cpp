#include "pinwindow/ClickThroughExitButton.h"
#include "platform/WindowLevel.h"
#include "cursor/CursorManager.h"
#include <QMouseEvent>
#include <QPainter>
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include <QShowEvent>
#include <QHideEvent>

namespace {
constexpr int kPaddingX = 12;
constexpr int kPaddingY = 5;
}

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

QString ClickThroughExitButton::text() const
{
    return tr("Click-through \u00b7 Turn off \u2715");
}

void ClickThroughExitButton::setupUI()
{
    // The shared glass surface (#225), not a local stylesheet.
    setAccessibleName(tr("Turn off click-through"));
    setToolTip(tr("Turn off click-through"));
    QFont labelFont = font();
    labelFont.setPixelSize(11);
    labelFont.setBold(true);
    setFont(labelFont);
    const QFontMetrics metrics(labelFont);
    setFixedSize(metrics.horizontalAdvance(text()) + 2 * kPaddingX, metrics.height() + 2 * kPaddingY);
}

void ClickThroughExitButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ToolbarStyleConfig& style = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, rect().adjusted(0, 0, -1, -1), style, height() / 2);
    painter.setPen(m_hovered ? style.iconHoveredColor : style.iconNormalColor);
    painter.drawText(rect(), Qt::AlignCenter, text());
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

void ClickThroughExitButton::setClickThroughEnabled(bool enabled)
{
    // Hiding the target must not discard the requested click-through state.
    m_clickThroughEnabled = enabled;
    if (enabled && m_targetWindow && m_targetWindow->isVisible()) {
        updatePosition();
        show();
    } else {
        hide();
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
        } else if (event->type() == QEvent::Show) {
            setClickThroughEnabled(m_clickThroughEnabled);
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
    m_hovered = true;
    update();
    CursorManager::instance().pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
    QWidget::enterEvent(event);
}

void ClickThroughExitButton::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
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
