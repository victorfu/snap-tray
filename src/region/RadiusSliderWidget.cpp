#include "region/RadiusSliderWidget.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"

#include <QPainter>
#include <QDebug>

RadiusSliderWidget::RadiusSliderWidget(QObject* parent)
    : QObject(parent)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    // Load icon
    IconRenderer::instance().loadIcon("radius", ":/icons/icons/radius.svg");
}

void RadiusSliderWidget::setRadiusRange(int min, int max)
{
    m_minRadius = min;
    m_maxRadius = max;
    if (m_currentRadius < m_minRadius) m_currentRadius = m_minRadius;
    if (m_currentRadius > m_maxRadius) m_currentRadius = m_maxRadius;
}

void RadiusSliderWidget::setCurrentRadius(int radius)
{
    m_currentRadius = qBound(m_minRadius, radius, m_maxRadius);
}

void RadiusSliderWidget::updatePosition(const QRect& anchorRect, int screenWidth)
{
    // Position to the right of the dimension info rect
    int widgetX = anchorRect.right() + GAP;
    int widgetY = anchorRect.top() + (anchorRect.height() - WIDGET_HEIGHT) / 2;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - WIDGET_WIDTH - 5;
        if (widgetX > maxX) {
            // Fall back to positioning below the dimension info
            widgetX = anchorRect.left();
            widgetY = anchorRect.bottom() + 4;
        }
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    m_widgetRect = QRect(widgetX, widgetY, WIDGET_WIDTH, WIDGET_HEIGHT);
    updateLayout();
}

void RadiusSliderWidget::updateLayout()
{
    int currentX = m_widgetRect.left() + PADDING;
    int centerY = m_widgetRect.center().y();

    // Icon on the left
    m_iconRect = QRect(currentX, centerY - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE);
    currentX += ICON_SIZE + 4;

    // Minus button
    m_minusButtonRect = QRect(currentX, centerY - BUTTON_SIZE / 2, BUTTON_SIZE, BUTTON_SIZE);
    currentX += BUTTON_SIZE + 4;

    // Value display on the right (before plus button)
    int valueX = m_widgetRect.right() - PADDING - VALUE_WIDTH;
    m_valueRect = QRect(valueX, m_widgetRect.top(), VALUE_WIDTH, WIDGET_HEIGHT);

    // Plus button (before value)
    int plusX = valueX - 4 - BUTTON_SIZE;
    m_plusButtonRect = QRect(plusX, centerY - BUTTON_SIZE / 2, BUTTON_SIZE, BUTTON_SIZE);

    // Slider track in the middle (between minus and plus buttons)
    int trackLeft = currentX;
    int trackRight = plusX - 4;
    int trackY = centerY - SLIDER_HEIGHT / 2;
    m_sliderTrackRect = QRect(trackLeft, trackY, trackRight - trackLeft, SLIDER_HEIGHT);
}

void RadiusSliderWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw glass panel background (6px radius for sub-widgets)
    GlassRenderer::drawGlassPanel(painter, m_widgetRect, m_styleConfig, 6);

    // Draw radius icon
    IconRenderer::instance().renderIcon(painter, m_iconRect, "radius", m_styleConfig.textColor, 0);

    // Set font for text elements
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    // Draw minus button
    QColor buttonBg = (m_hoveredButton == 0) ? m_styleConfig.hoverBackgroundColor : m_styleConfig.buttonInactiveColor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(buttonBg);
    painter.drawRoundedRect(m_minusButtonRect, 4, 4);
    painter.setPen(m_styleConfig.textColor);
    painter.drawText(m_minusButtonRect, Qt::AlignCenter, "-");

    // Draw plus button
    buttonBg = (m_hoveredButton == 1) ? m_styleConfig.hoverBackgroundColor : m_styleConfig.buttonInactiveColor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(buttonBg);
    painter.drawRoundedRect(m_plusButtonRect, 4, 4);
    painter.setPen(m_styleConfig.textColor);
    painter.drawText(m_plusButtonRect, Qt::AlignCenter, "+");

    // Draw slider track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_styleConfig.buttonInactiveColor);
    painter.drawRoundedRect(m_sliderTrackRect, SLIDER_HEIGHT / 2, SLIDER_HEIGHT / 2);

    // Draw slider track fill (from left to handle position)
    int handleX = radiusToPosition(m_currentRadius);
    QRect fillRect(m_sliderTrackRect.left(), m_sliderTrackRect.top(),
                   handleX - m_sliderTrackRect.left(), SLIDER_HEIGHT);
    painter.setBrush(QColor(0, 174, 255));
    painter.drawRoundedRect(fillRect, SLIDER_HEIGHT / 2, SLIDER_HEIGHT / 2);

    // Draw handle
    int handleY = m_sliderTrackRect.center().y();
    QRect handleRect(handleX - HANDLE_SIZE / 2, handleY - HANDLE_SIZE / 2,
                     HANDLE_SIZE, HANDLE_SIZE);

    // Handle shadow
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawEllipse(handleRect.adjusted(1, 1, 1, 1));

    // Handle fill
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor(0, 174, 255), 2));
    painter.drawEllipse(handleRect);

    // Draw value label
    painter.setPen(m_styleConfig.textColor);
    QString label = QString::number(m_currentRadius);
    painter.drawText(m_valueRect, Qt::AlignCenter, label);

    // Draw tooltip when hovered
    drawTooltip(painter);
}

void RadiusSliderWidget::drawTooltip(QPainter& painter)
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

bool RadiusSliderWidget::contains(const QPoint& pos) const
{
    return m_widgetRect.contains(pos);
}

bool RadiusSliderWidget::handleMousePress(const QPoint& pos)
{
    if (!m_visible || !m_widgetRect.contains(pos)) {
        return false;
    }

    // Check minus button
    if (m_minusButtonRect.contains(pos)) {
        int newRadius = qMax(m_minRadius, m_currentRadius - 5);
        if (newRadius != m_currentRadius) {
            m_currentRadius = newRadius;
            emit radiusChanged(m_currentRadius);
        }
        return true;
    }

    // Check plus button
    if (m_plusButtonRect.contains(pos)) {
        int newRadius = qMin(m_maxRadius, m_currentRadius + 5);
        if (newRadius != m_currentRadius) {
            m_currentRadius = newRadius;
            emit radiusChanged(m_currentRadius);
        }
        return true;
    }

    // Check slider track area
    QRect sliderArea = m_sliderTrackRect.adjusted(-HANDLE_SIZE / 2, -HANDLE_SIZE,
                                                   HANDLE_SIZE / 2, HANDLE_SIZE);
    if (sliderArea.contains(pos)) {
        m_isDragging = true;
        int newRadius = positionToRadius(pos.x());
        if (newRadius != m_currentRadius) {
            m_currentRadius = newRadius;
            emit radiusChanged(m_currentRadius);
        }
        return true;
    }

    return true;  // Consume click if on widget
}

bool RadiusSliderWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    if (!m_visible) return false;

    bool needsUpdate = false;

    if (m_isDragging && pressed) {
        int newRadius = positionToRadius(pos.x());
        if (newRadius != m_currentRadius) {
            m_currentRadius = newRadius;
            emit radiusChanged(m_currentRadius);
            needsUpdate = true;
        }
    }

    // Update overall hover state
    bool hovered = m_widgetRect.contains(pos);
    if (hovered != m_hovered) {
        m_hovered = hovered;
        needsUpdate = true;
    }

    // Update hover state for buttons
    int newHovered = -1;
    if (m_minusButtonRect.contains(pos)) {
        newHovered = 0;
    } else if (m_plusButtonRect.contains(pos)) {
        newHovered = 1;
    }

    if (newHovered != m_hoveredButton) {
        m_hoveredButton = newHovered;
        needsUpdate = true;
    }

    return needsUpdate;
}

bool RadiusSliderWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);

    if (m_isDragging) {
        m_isDragging = false;
        return true;
    }
    return false;
}

int RadiusSliderWidget::positionToRadius(int x) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    if (x <= trackLeft) return m_minRadius;
    if (x >= trackRight) return m_maxRadius;

    double ratio = static_cast<double>(x - trackLeft) / (trackRight - trackLeft);
    int radius = m_minRadius + static_cast<int>(ratio * (m_maxRadius - m_minRadius) + 0.5);
    return qBound(m_minRadius, radius, m_maxRadius);
}

int RadiusSliderWidget::radiusToPosition(int radius) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    double ratio = static_cast<double>(radius - m_minRadius) / (m_maxRadius - m_minRadius);
    return trackLeft + static_cast<int>(ratio * (trackRight - trackLeft));
}

QString RadiusSliderWidget::tooltipText() const
{
    return tr("Corner Radius");
}
