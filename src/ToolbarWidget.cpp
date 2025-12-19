#include "ToolbarWidget.h"
#include "IconRenderer.h"

#include <QPainter>
#include <QLinearGradient>
#include <QDebug>

ToolbarWidget::ToolbarWidget(QObject* parent)
    : QObject(parent)
    , m_activeButton(-1)
    , m_hoveredButton(-1)
{
    // Default icon color provider
    m_iconColorProvider = [](int buttonId, bool isActive, bool isHovered) -> QColor {
        Q_UNUSED(buttonId);
        if (isActive) {
            return Qt::white;
        }
        return QColor(220, 220, 220);
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
    // Count separators
    int separatorCount = 0;
    for (const auto& btn : m_buttons) {
        if (btn.separatorBefore) {
            separatorCount++;
        }
    }

    int toolbarWidth = m_buttons.size() * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * SEPARATOR_WIDTH;
    int toolbarX = centerX - toolbarWidth / 2;
    int toolbarY = bottomY - TOOLBAR_HEIGHT;

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);
    updateButtonRects();
}

void ToolbarWidget::setPositionForSelection(const QRect& referenceRect, int viewportHeight)
{
    // Count separators
    int separatorCount = 0;
    for (const auto& btn : m_buttons) {
        if (btn.separatorBefore) {
            separatorCount++;
        }
    }

    int toolbarWidth = m_buttons.size() * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * SEPARATOR_WIDTH;

    QRect sel = referenceRect.normalized();

    // Position below selection, right-aligned
    int toolbarX = sel.right() - toolbarWidth + 1;
    int toolbarY = sel.bottom() + 8;

    // If toolbar would go off screen, position above selection instead
    if (toolbarY + TOOLBAR_HEIGHT > viewportHeight - 5) {
        toolbarY = sel.top() - TOOLBAR_HEIGHT - 8;
    }

    // Keep on screen horizontally
    if (toolbarX < 5) toolbarX = 5;

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);
    updateButtonRects();
}

void ToolbarWidget::updateButtonRects()
{
    if (m_buttons.isEmpty()) return;

    int x = m_toolbarRect.left() + 10;
    int y = m_toolbarRect.top() + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < m_buttons.size(); ++i) {
        // Add separator space
        if (m_buttons[i].separatorBefore) {
            x += SEPARATOR_WIDTH;
        }

        m_buttonRects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;
    }
}

void ToolbarWidget::draw(QPainter& painter)
{
    // Draw shadow
    QRect shadowRect = m_toolbarRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 8, 8);

    // Draw toolbar background with gradient
    QLinearGradient gradient(m_toolbarRect.topLeft(), m_toolbarRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_toolbarRect, 8, 8);

    // Render buttons
    for (int i = 0; i < m_buttons.size(); ++i) {
        QRect btnRect = m_buttonRects[i];
        const ButtonConfig& config = m_buttons[i];

        // Check if this button is an "active" type
        bool isActiveType = m_activeButtonIds.contains(config.id);
        bool isActive = isActiveType && (config.id == m_activeButton);
        bool isHovered = (i == m_hoveredButton);

        // Highlight active tool or hovered button
        if (isActive) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 200));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        } else if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        // Draw separator before this button
        if (config.separatorBefore) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
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
    // Note: Right boundary check would need viewport width

    textRect.moveTo(tooltipX, tooltipY);

    // Draw tooltip background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip border
    painter.setPen(QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

int ToolbarWidget::buttonAtPosition(const QPoint& pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
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
