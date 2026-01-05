#include "region/AspectRatioWidget.h"
#include "GlassRenderer.h"

#include <QPainter>

AspectRatioWidget::AspectRatioWidget(QObject* parent)
    : QObject(parent)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
}

void AspectRatioWidget::setLocked(bool locked)
{
    m_locked = locked;
}

void AspectRatioWidget::setLockedRatio(int width, int height)
{
    if (width > 0 && height > 0) {
        m_ratioWidth = width;
        m_ratioHeight = height;
    }
}

void AspectRatioWidget::updatePosition(const QRect& anchorRect, int screenWidth, int screenHeight)
{
    int widgetX = anchorRect.right() + GAP;
    int widgetY = anchorRect.top() + (anchorRect.height() - WIDGET_HEIGHT) / 2;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - WIDGET_WIDTH - 5;
        if (widgetX > maxX) {
            // Fall back to positioning below the anchor
            widgetX = anchorRect.left();
            widgetY = anchorRect.bottom() + 4;
        }
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    m_widgetRect = QRect(widgetX, widgetY, WIDGET_WIDTH, WIDGET_HEIGHT);
    Q_UNUSED(screenHeight);
    updateLayout();
}

void AspectRatioWidget::updateLayout()
{
    m_textRect = m_widgetRect.adjusted(PADDING, 0, -PADDING, 0);
}

QString AspectRatioWidget::labelText() const
{
    if (!m_locked) {
        return "Free";
    }
    return QString("Lock %1:%2").arg(m_ratioWidth).arg(m_ratioHeight);
}

void AspectRatioWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    GlassRenderer::drawGlassPanel(painter, m_widgetRect, m_styleConfig, 6);

    if (m_locked || m_hovered) {
        QColor bg = m_locked ? m_styleConfig.activeBackgroundColor : m_styleConfig.hoverBackgroundColor;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(m_widgetRect.adjusted(2, 2, -2, -2), 4, 4);
    }

    painter.setPen(m_styleConfig.textColor);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    painter.drawText(m_textRect, Qt::AlignVCenter | Qt::AlignLeft, labelText());
}

bool AspectRatioWidget::contains(const QPoint& pos) const
{
    return m_widgetRect.contains(pos);
}

bool AspectRatioWidget::handleMousePress(const QPoint& pos)
{
    if (!m_visible) return false;

    if (m_widgetRect.contains(pos)) {
        m_locked = !m_locked;
        emit lockChanged(m_locked);
        return true;
    }
    return false;
}

bool AspectRatioWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    Q_UNUSED(pressed);
    if (!m_visible) return false;

    bool hovered = m_widgetRect.contains(pos);
    if (hovered != m_hovered) {
        m_hovered = hovered;
        return true;
    }

    return false;
}

bool AspectRatioWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    return false;
}
