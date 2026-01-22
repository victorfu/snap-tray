#include "ui/sections/ShapeSection.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"

#include <QPainter>
#include <QPen>

ShapeSection::ShapeSection(QObject* parent)
    : QObject(parent)
{
    m_buttonRects.resize(3);
}

void ShapeSection::setShapeType(ShapeType type)
{
    if (m_shapeType != type) {
        m_shapeType = type;
        emit shapeTypeChanged(m_shapeType);
    }
}

void ShapeSection::setShapeFillMode(ShapeFillMode mode)
{
    if (m_shapeFillMode != mode) {
        m_shapeFillMode = mode;
        emit shapeFillModeChanged(m_shapeFillMode);
    }
}

int ShapeSection::preferredWidth() const
{
    return BUTTON_SIZE * 3 + BUTTON_SPACING * 2 + SECTION_PADDING;
}

void ShapeSection::updateLayout(int containerTop, int containerHeight, int xOffset)
{
    m_sectionRect = QRect(xOffset, containerTop, preferredWidth(), containerHeight);

    // Center buttons vertically
    int buttonY = containerTop + (containerHeight - BUTTON_SIZE) / 2;

    for (int i = 0; i < 3; ++i) {
        m_buttonRects[i] = QRect(
            xOffset + SECTION_PADDING / 2 + i * (BUTTON_SIZE + BUTTON_SPACING),
            buttonY,
            BUTTON_SIZE,
            BUTTON_SIZE
        );
    }
}

void ShapeSection::draw(QPainter& painter, const ToolbarStyleConfig& styleConfig)
{
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < 3; ++i) {
        if (i >= m_buttonRects.size()) break;

        QRect rect = m_buttonRects[i];

        // Determine selection state
        bool isSelected;
        if (i < 2) {
            // Shape type buttons (0=Rectangle, 1=Ellipse)
            isSelected = (m_shapeType == (i == 0 ? ShapeType::Rectangle : ShapeType::Ellipse));
        } else {
            // Fill mode toggle
            isSelected = (m_shapeFillMode == ShapeFillMode::Filled);
        }
        bool isHovered = (m_hoveredButton == i);

        // Background
        QColor bgColor;
        if (isSelected) {
            bgColor = styleConfig.buttonActiveColor;
        } else if (isHovered) {
            bgColor = styleConfig.buttonHoverColor;
        } else {
            bgColor = styleConfig.buttonInactiveColor;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect, 4, 4);

        // Draw icon based on button index
        QColor iconColor = isSelected ? styleConfig.textActiveColor : styleConfig.textColor;

        QString iconKey;
        switch (i) {
        case 0:
            iconKey = "rectangle";
            break;
        case 1:
            iconKey = "ellipse";
            break;
        case 2:
            iconKey = (m_shapeFillMode == ShapeFillMode::Filled) ? "shape-filled" : "shape-outline";
            break;
        }
        IconRenderer::instance().renderIcon(painter, rect, iconKey, iconColor);
    }
}

bool ShapeSection::contains(const QPoint& pos) const
{
    return m_sectionRect.contains(pos);
}

int ShapeSection::buttonAtPosition(const QPoint& pos) const
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool ShapeSection::handleClick(const QPoint& pos)
{
    int btn = buttonAtPosition(pos);
    if (btn >= 0) {
        if (btn < 2) {
            // Shape type buttons (0=Rectangle, 1=Ellipse)
            m_shapeType = (btn == 0) ? ShapeType::Rectangle : ShapeType::Ellipse;
            emit shapeTypeChanged(m_shapeType);
        } else {
            // Button 2: Toggle fill mode
            m_shapeFillMode = (m_shapeFillMode == ShapeFillMode::Outline)
                ? ShapeFillMode::Filled : ShapeFillMode::Outline;
            emit shapeFillModeChanged(m_shapeFillMode);
        }
        return true;
    }
    return false;
}

bool ShapeSection::updateHovered(const QPoint& pos)
{
    int newHovered = buttonAtPosition(pos);
    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        return true;
    }
    return false;
}
