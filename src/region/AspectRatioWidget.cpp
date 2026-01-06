#include "region/AspectRatioWidget.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"

#include <QPainter>

AspectRatioWidget::AspectRatioWidget(QObject* parent)
    : QObject(parent)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    // Load icons
    IconRenderer::instance().loadIcon("ratio-free", ":/icons/icons/ratio-free.svg");
    IconRenderer::instance().loadIcon("ratio-lock", ":/icons/icons/ratio-lock.svg");
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
    int widgetWidth = m_locked ? WIDGET_WIDTH_LOCKED : WIDGET_WIDTH_FREE;
    int widgetX = anchorRect.right() + GAP;
    int widgetY = anchorRect.top() + (anchorRect.height() - WIDGET_HEIGHT) / 2;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - widgetWidth - 5;
        if (widgetX > maxX) {
            // Fall back to positioning below the anchor
            widgetX = anchorRect.left();
            widgetY = anchorRect.bottom() + 4;
        }
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    m_widgetRect = QRect(widgetX, widgetY, widgetWidth, WIDGET_HEIGHT);
    Q_UNUSED(screenHeight);
    updateLayout();
}

void AspectRatioWidget::updateLayout()
{
    int centerY = m_widgetRect.center().y();
    int currentX = m_widgetRect.left() + PADDING;

    // Icon on the left
    m_iconRect = QRect(currentX, centerY - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE);
    currentX += ICON_SIZE + 4;

    // Text rect for ratio (only used when locked)
    m_textRect = QRect(currentX, m_widgetRect.top(), m_widgetRect.right() - PADDING - currentX, WIDGET_HEIGHT);
}

QString AspectRatioWidget::labelText() const
{
    // Returns ratio text (only displayed when locked)
    return QString("%1:%2").arg(m_ratioWidth).arg(m_ratioHeight);
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

    // Draw appropriate icon based on lock state
    QString iconKey = m_locked ? "ratio-lock" : "ratio-free";
    IconRenderer::instance().renderIcon(painter, m_iconRect, iconKey, m_styleConfig.textColor, 0);

    // Draw ratio text only when locked
    if (m_locked) {
        painter.setPen(m_styleConfig.textColor);
        QFont font = painter.font();
        font.setPointSize(11);
        painter.setFont(font);
        painter.drawText(m_textRect, Qt::AlignVCenter | Qt::AlignLeft, labelText());
    }

    // Draw tooltip when hovered
    drawTooltip(painter);
}

void AspectRatioWidget::drawTooltip(QPainter& painter)
{
    if (!m_hovered) return;

    QString tooltip = tooltipText();
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);
    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip).adjusted(-8, -4, 8, 4);

    // Position tooltip above the widget
    int x = m_widgetRect.center().x() - textRect.width() / 2;
    int y = m_widgetRect.top() - textRect.height() - 6;
    textRect.moveTo(x, y);

    // Draw glass panel without shadow
    ToolbarStyleConfig tooltipConfig = m_styleConfig;
    tooltipConfig.shadowColor.setAlpha(0);
    tooltipConfig.shadowOffsetY = 0;
    tooltipConfig.shadowBlurRadius = 0;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 6);

    // Draw tooltip text
    painter.setPen(m_styleConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
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

QString AspectRatioWidget::tooltipText() const
{
    if (m_locked) {
        return tr("Lock aspect ratio (%1:%2)").arg(m_ratioWidth).arg(m_ratioHeight);
    }
    return tr("Free aspect ratio");
}
