#include "ui/sections/AutoBlurSection.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QPen>
#include <QFontMetrics>

AutoBlurSection::AutoBlurSection(QObject* parent)
    : QObject(parent)
{
}

void AutoBlurSection::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void AutoBlurSection::setProcessing(bool processing)
{
    m_processing = processing;
}

int AutoBlurSection::preferredWidth() const
{
    return BUTTON_SIZE + SECTION_PADDING;
}

void AutoBlurSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, preferredWidth(), containerHeight);

    // Center button vertically
    int buttonY = containerTop + (containerHeight - BUTTON_SIZE) / 2;
    m_buttonRect = QRect(xOffset + SECTION_PADDING / 2, buttonY, BUTTON_SIZE, BUTTON_SIZE);
}

void AutoBlurSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw button background only when processing or hovered
    if (m_processing) {
        // Yellow/orange when processing
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 193, 7));  // Material amber
        painter.drawRoundedRect(m_buttonRect, 4, 4);
    } else if (m_hovered && m_enabled) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(styleConfig.buttonHoverColor);
        painter.drawRoundedRect(m_buttonRect, 4, 4);
    }
    // Normal state: no background (flat appearance)

    // Draw icon
    QColor iconColor;
    if (m_processing) {
        iconColor = Qt::black;  // Dark icon on yellow background
    } else if (!m_enabled) {
        // Dimmed icon color
        iconColor = styleConfig.textColor;
        iconColor.setAlpha(100);
    } else {
        iconColor = styleConfig.textColor;
    }

    IconRenderer::instance().renderIcon(painter, m_buttonRect, "auto-blur", iconColor);

    // Draw tooltip when hovered
    if (m_hovered && !m_tooltip.isEmpty()) {
        drawTooltip(painter, styleConfig);
    }
}

void AutoBlurSection::drawTooltip(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(m_tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the button
    int tooltipX = m_buttonRect.center().x() - textRect.width() / 2;
    int tooltipY = m_sectionRect.top() - textRect.height() - 6;
    textRect.moveTo(tooltipX, tooltipY);

    // Draw glass tooltip
    ToolbarStyleConfig tooltipConfig = styleConfig;
    tooltipConfig.shadowOffsetY = 2;
    tooltipConfig.shadowBlurRadius = 6;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 6);

    // Draw tooltip text
    painter.setPen(styleConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, m_tooltip);
}

bool AutoBlurSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

bool AutoBlurSection::handleClick(const QPoint& pos)
{
    if (m_buttonRect.contains(pos) && m_enabled && !m_processing) {
        emit autoBlurRequested();
        return true;
    }
    return false;
}

bool AutoBlurSection::updateHovered(const QPoint& pos)
{
    bool newHovered = m_buttonRect.contains(pos);
    if (newHovered != m_hovered) {
        m_hovered = newHovered;
        return true;
    }
    return false;
}

void AutoBlurSection::resetHoverState()
{
    m_hovered = false;
}
