#include "ToolbarWidget.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "toolbar/ToolbarRenderer.h"

#include <QPainter>
#include <QLinearGradient>
#include <QDebug>

ToolbarWidget::ToolbarWidget(QObject* parent)
    : QObject(parent)
    , m_activeButton(-1)
    , m_hoveredButton(-1)
    , m_viewportWidth(0)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    // Default icon color provider using style config
    m_iconColorProvider = [this](int buttonId, bool isActive, bool isHovered) -> QColor {
        Q_UNUSED(buttonId);
        Q_UNUSED(isHovered);
        if (isActive) {
            return m_styleConfig.iconActiveColor;
        }
        return m_styleConfig.iconNormalColor;
    };
}

void ToolbarWidget::setButtons(const QVector<ButtonConfig>& buttons)
{
    m_buttons = buttons;
    m_buttonRects.resize(buttons.size());
    updateButtonRects();
}

void ToolbarWidget::setActiveButton(int buttonId)
{
    m_activeButton = buttonId;
}

void ToolbarWidget::setIconColorProvider(const IconColorProvider& provider)
{
    m_iconColorProvider = provider;
}

void ToolbarWidget::setPosition(int centerX, int bottomY)
{
    int toolbarWidth = Toolbar::ToolbarRenderer::calculateWidth(m_buttons);
    m_toolbarRect = Toolbar::ToolbarRenderer::positionAtBottom(centerX, bottomY, toolbarWidth);
    updateButtonRects();
}

void ToolbarWidget::setPositionForSelection(const QRect& referenceRect, int viewportHeight)
{
    int toolbarWidth = Toolbar::ToolbarRenderer::calculateWidth(m_buttons);
    m_toolbarRect = Toolbar::ToolbarRenderer::positionBelow(referenceRect, toolbarWidth,
                                                             m_viewportWidth, viewportHeight);
    updateButtonRects();
}

void ToolbarWidget::updateButtonRects()
{
    m_buttonRects = Toolbar::ToolbarRenderer::calculateButtonRects(m_toolbarRect, m_buttons);
}

void ToolbarWidget::draw(QPainter& painter)
{
    // Draw glass panel (background, border, highlight; no shadow)
    ToolbarStyleConfig panelConfig = m_styleConfig;
    panelConfig.shadowColor.setAlpha(0);
    panelConfig.shadowOffsetY = 0;
    panelConfig.shadowBlurRadius = 0;
    GlassRenderer::drawGlassPanel(painter, m_toolbarRect, panelConfig);

    // Render buttons
    for (int i = 0; i < m_buttons.size(); ++i) {
        QRect btnRect = m_buttonRects[i];
        const ButtonConfig& config = m_buttons[i];

        // Check if this button is an "active" type
        bool isActiveType = m_activeButtonIds.contains(config.id);
        bool isActive = isActiveType && (config.id == m_activeButton);
        bool isHovered = (i == m_hoveredButton);

        // Draw separator before this button
        if (config.separatorBefore) {
            painter.setPen(m_styleConfig.separatorColor);
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Highlight active tool or hovered button
        if (isActive) {
            if (m_styleConfig.useRedDotIndicator) {
                // Light style: draw red dot at top-right corner
                int dotRadius = m_styleConfig.redDotSize / 2;
                int dotX = btnRect.right() - dotRadius - 2;
                int dotY = btnRect.top() + dotRadius + 2;
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_styleConfig.redDotColor);
                painter.drawEllipse(QPoint(dotX, dotY), dotRadius, dotRadius);
            } else {
                // Dark style: background highlight
                painter.setPen(Qt::NoPen);
                painter.setBrush(m_styleConfig.activeBackgroundColor);
                painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 6, 6);
            }
        } else if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        // Get icon color and render
        QColor iconColor = getIconColor(config.id, isActive, isHovered);
        IconRenderer::instance().renderIcon(painter, btnRect, config.iconKey, iconColor);
    }
}

void ToolbarWidget::drawTooltip(QPainter& painter)
{
    if (m_hoveredButton < 0 || m_hoveredButton >= m_buttons.size()) return;

    QString tooltip = m_buttons[m_hoveredButton].tooltip;
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the toolbar
    QRect btnRect = m_buttonRects[m_hoveredButton];
    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = m_toolbarRect.top() - textRect.height() - 6;

    // Keep on screen
    if (tooltipX < 5) tooltipX = 5;
    if (m_viewportWidth > 0 && tooltipX + textRect.width() > m_viewportWidth - 5) {
        tooltipX = m_viewportWidth - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    // Draw glass tooltip without shadow
    ToolbarStyleConfig tooltipConfig = m_styleConfig;
    tooltipConfig.shadowColor.setAlpha(0);
    tooltipConfig.shadowOffsetY = 0;
    tooltipConfig.shadowBlurRadius = 0;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 6);

    // Draw tooltip text
    painter.setPen(m_styleConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

int ToolbarWidget::buttonAtPosition(const QPoint& pos) const
{
    return Toolbar::ToolbarRenderer::buttonAtPosition(pos, m_buttonRects);
}

bool ToolbarWidget::updateHoveredButton(const QPoint& pos)
{
    int newHovered = buttonAtPosition(pos);
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        return true;
    }
    return false;
}

bool ToolbarWidget::contains(const QPoint& pos) const
{
    return m_toolbarRect.contains(pos);
}

QColor ToolbarWidget::getIconColor(int buttonId, bool isActive, bool isHovered) const
{
    if (m_iconColorProvider) {
        return m_iconColorProvider(buttonId, isActive, isHovered);
    }
    return isActive ? Qt::white : QColor(220, 220, 220);
}

int ToolbarWidget::buttonIdAt(int index) const
{
    if (index >= 0 && index < m_buttons.size()) {
        return m_buttons[index].id;
    }
    return -1;
}

void ToolbarWidget::setStyle(ToolbarStyleType type)
{
    m_styleConfig = ToolbarStyleConfig::getStyle(type);
}

void ToolbarWidget::setStyleConfig(const ToolbarStyleConfig& config)
{
    m_styleConfig = config;
}
