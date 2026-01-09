#include "region/RegionControlWidget.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"

#include <QPainter>

RegionControlWidget::RegionControlWidget(QObject* parent)
    : QObject(parent)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    // Load icons
    IconRenderer::instance().loadIcon("radius", ":/icons/icons/radius.svg");
    IconRenderer::instance().loadIcon("ratio-free", ":/icons/icons/ratio-free.svg");
    IconRenderer::instance().loadIcon("ratio-lock", ":/icons/icons/ratio-lock.svg");
}

// =============================================================================
// Radius Methods
// =============================================================================

void RegionControlWidget::setRadiusRange(int min, int max)
{
    m_minRadius = min;
    m_maxRadius = max;
    if (m_currentRadius < m_minRadius) m_currentRadius = m_minRadius;
    if (m_currentRadius > m_maxRadius) m_currentRadius = m_maxRadius;
}

void RegionControlWidget::setCurrentRadius(int radius)
{
    m_currentRadius = qBound(m_minRadius, radius, m_maxRadius);
}

void RegionControlWidget::setRadiusEnabled(bool enabled)
{
    if (m_radiusEnabled == enabled) return;

    m_radiusEnabled = enabled;
    emit radiusEnabledChanged(m_radiusEnabled);

    if (!m_radiusEnabled) {
        // When disabled, force radius to 0
        if (m_currentRadius != 0) {
            m_currentRadius = 0;
            emit radiusChanged(0);
        }
    }
}

// =============================================================================
// Aspect Ratio Methods
// =============================================================================

void RegionControlWidget::setLocked(bool locked)
{
    m_ratioLocked = locked;
}

void RegionControlWidget::setLockedRatio(int width, int height)
{
    if (width > 0 && height > 0) {
        m_ratioWidth = width;
        m_ratioHeight = height;
    }
}

// =============================================================================
// Positioning
// =============================================================================

void RegionControlWidget::updatePosition(const QRect& anchorRect, int screenWidth, int screenHeight)
{
    int ratioWidth = m_ratioLocked ? RATIO_WIDTH_LOCKED : RATIO_WIDTH_FREE;
    int totalWidth = TOGGLE_SIZE + SECTION_SPACING + ratioWidth;

    int widgetX = anchorRect.right() + GAP;
    int widgetY = anchorRect.top() + (anchorRect.height() - WIDGET_HEIGHT) / 2;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - totalWidth - 5;
        if (widgetX > maxX) {
            // Fall back to positioning below the anchor
            widgetX = anchorRect.left();
            widgetY = anchorRect.bottom() + 4;
        }
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    m_widgetRect = QRect(widgetX, widgetY, totalWidth, WIDGET_HEIGHT);
    Q_UNUSED(screenHeight);
    updateLayout();
}

void RegionControlWidget::updateLayout()
{
    int centerY = m_widgetRect.center().y();

    // Radius toggle on the left
    m_radiusToggleRect = QRect(m_widgetRect.left(), m_widgetRect.top(),
                                TOGGLE_SIZE, WIDGET_HEIGHT);
    m_radiusIconRect = QRect(m_radiusToggleRect.center().x() - ICON_SIZE / 2,
                              centerY - ICON_SIZE / 2,
                              ICON_SIZE, ICON_SIZE);

    // Ratio section on the right
    int ratioWidth = m_ratioLocked ? RATIO_WIDTH_LOCKED : RATIO_WIDTH_FREE;
    m_ratioSectionRect = QRect(m_radiusToggleRect.right() + SECTION_SPACING,
                                m_widgetRect.top(),
                                ratioWidth, WIDGET_HEIGHT);

    int ratioIconX = m_ratioSectionRect.left() + PADDING;
    m_ratioIconRect = QRect(ratioIconX, centerY - ICON_SIZE / 2, ICON_SIZE, ICON_SIZE);

    if (m_ratioLocked) {
        int textX = ratioIconX + ICON_SIZE + 4;
        m_ratioTextRect = QRect(textX, m_widgetRect.top(),
                                 m_ratioSectionRect.right() - PADDING - textX,
                                 WIDGET_HEIGHT);
    }

    // Slider popup - positioned above the radius toggle
    int popupX = m_radiusToggleRect.center().x() - POPUP_WIDTH / 2;
    int popupY = m_widgetRect.top() - POPUP_GAP - POPUP_HEIGHT;

    // Keep popup on screen
    if (popupX < 5) popupX = 5;

    m_sliderPopupRect = QRect(popupX, popupY, POPUP_WIDTH, POPUP_HEIGHT);

    // Layout slider popup internals
    int sliderCenterY = m_sliderPopupRect.center().y();
    int currentX = m_sliderPopupRect.left() + PADDING;

    // Minus button
    m_sliderMinusRect = QRect(currentX, sliderCenterY - BUTTON_SIZE / 2,
                               BUTTON_SIZE, BUTTON_SIZE);
    currentX += BUTTON_SIZE + 4;

    // Value display on the right
    int valueX = m_sliderPopupRect.right() - PADDING - VALUE_WIDTH;
    m_sliderValueRect = QRect(valueX, m_sliderPopupRect.top(), VALUE_WIDTH, POPUP_HEIGHT);

    // Plus button (before value)
    int plusX = valueX - 4 - BUTTON_SIZE;
    m_sliderPlusRect = QRect(plusX, sliderCenterY - BUTTON_SIZE / 2,
                              BUTTON_SIZE, BUTTON_SIZE);

    // Slider track in the middle
    int trackLeft = currentX;
    int trackRight = plusX - 4;
    int trackY = sliderCenterY - SLIDER_HEIGHT / 2;
    m_sliderTrackRect = QRect(trackLeft, trackY, trackRight - trackLeft, SLIDER_HEIGHT);
}

// =============================================================================
// Rendering
// =============================================================================

void RegionControlWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw shared glass panel background
    GlassRenderer::drawGlassPanel(painter, m_widgetRect, m_styleConfig, 6);

    // Draw radius toggle
    drawRadiusToggle(painter);

    // Draw ratio section
    drawRatioSection(painter);

    // Draw slider popup if visible
    if (m_sliderPopupVisible) {
        drawSliderPopup(painter);
    }

    painter.restore();
}

void RegionControlWidget::drawRadiusToggle(QPainter& painter)
{
    // Draw active/hover background
    if (m_radiusEnabled || m_radiusHovered || m_sliderPopupVisible) {
        QColor bg = m_radiusEnabled ? m_styleConfig.activeBackgroundColor
                                    : m_styleConfig.hoverBackgroundColor;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(m_radiusToggleRect.adjusted(2, 2, -2, -2), 4, 4);
    }

    // Draw radius icon - use active color when enabled
    QColor iconColor = m_radiusEnabled ? m_styleConfig.iconActiveColor : m_styleConfig.textColor;
    if (!m_radiusEnabled) {
        iconColor.setAlphaF(0.4);  // Dimmed when disabled
    }
    IconRenderer::instance().renderIcon(painter, m_radiusIconRect, "radius", iconColor, 0);
}

void RegionControlWidget::drawRatioSection(QPainter& painter)
{
    // Draw hover/active background
    if (m_ratioLocked || m_ratioHovered) {
        QColor bg = m_ratioLocked ? m_styleConfig.activeBackgroundColor
                                  : m_styleConfig.hoverBackgroundColor;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(m_ratioSectionRect.adjusted(2, 2, -2, -2), 4, 4);
    }

    // Draw appropriate icon based on lock state
    QString iconKey = m_ratioLocked ? "ratio-lock" : "ratio-free";
    QColor iconColor = m_ratioLocked ? m_styleConfig.iconActiveColor : m_styleConfig.textColor;
    IconRenderer::instance().renderIcon(painter, m_ratioIconRect, iconKey,
                                         iconColor, 0);

    // Draw ratio text only when locked
    if (m_ratioLocked) {
        painter.setPen(m_styleConfig.textActiveColor);
        QFont font = painter.font();
        font.setPointSize(11);
        painter.setFont(font);
        painter.drawText(m_ratioTextRect, Qt::AlignVCenter | Qt::AlignLeft, ratioLabelText());
    }
}

void RegionControlWidget::drawSliderPopup(QPainter& painter)
{
    // Draw popup glass panel
    GlassRenderer::drawGlassPanel(painter, m_sliderPopupRect, m_styleConfig, 6);

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    // Draw minus button
    QColor buttonBg = (m_hoveredSliderButton == 0)
                      ? m_styleConfig.hoverBackgroundColor
                      : m_styleConfig.buttonInactiveColor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(buttonBg);
    painter.drawRoundedRect(m_sliderMinusRect, 4, 4);
    painter.setPen(m_styleConfig.textColor);
    painter.drawText(m_sliderMinusRect, Qt::AlignCenter, "-");

    // Draw plus button
    buttonBg = (m_hoveredSliderButton == 1)
               ? m_styleConfig.hoverBackgroundColor
               : m_styleConfig.buttonInactiveColor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(buttonBg);
    painter.drawRoundedRect(m_sliderPlusRect, 4, 4);
    painter.setPen(m_styleConfig.textColor);
    painter.drawText(m_sliderPlusRect, Qt::AlignCenter, "+");

    // Draw slider track background
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_styleConfig.buttonInactiveColor);
    painter.drawRoundedRect(m_sliderTrackRect, SLIDER_HEIGHT / 2, SLIDER_HEIGHT / 2);

    // Draw slider track fill
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
    painter.drawText(m_sliderValueRect, Qt::AlignCenter, QString::number(m_currentRadius));
}

QString RegionControlWidget::ratioLabelText() const
{
    return QString("%1:%2").arg(m_ratioWidth).arg(m_ratioHeight);
}

// =============================================================================
// Event Handling
// =============================================================================

bool RegionControlWidget::contains(const QPoint& pos) const
{
    if (!m_visible) return false;

    if (m_widgetRect.contains(pos)) return true;
    if (m_sliderPopupVisible && m_sliderPopupRect.contains(pos)) return true;

    return false;
}

bool RegionControlWidget::handleMousePress(const QPoint& pos)
{
    if (!m_visible) return false;

    // Handle slider popup clicks
    if (m_sliderPopupVisible && m_sliderPopupRect.contains(pos)) {
        // Minus button
        if (m_sliderMinusRect.contains(pos)) {
            int newRadius = qMax(m_minRadius, m_currentRadius - 5);
            if (newRadius != m_currentRadius) {
                m_currentRadius = newRadius;
                emit radiusChanged(m_currentRadius);
            }
            return true;
        }

        // Plus button
        if (m_sliderPlusRect.contains(pos)) {
            int newRadius = qMin(m_maxRadius, m_currentRadius + 5);
            if (newRadius != m_currentRadius) {
                m_currentRadius = newRadius;
                emit radiusChanged(m_currentRadius);
            }
            return true;
        }

        // Slider track
        QRect sliderArea = m_sliderTrackRect.adjusted(-HANDLE_SIZE / 2, -HANDLE_SIZE,
                                                       HANDLE_SIZE / 2, HANDLE_SIZE);
        if (sliderArea.contains(pos)) {
            m_isDraggingSlider = true;
            int newRadius = positionToRadius(pos.x());
            if (newRadius != m_currentRadius) {
                m_currentRadius = newRadius;
                emit radiusChanged(m_currentRadius);
            }
            return true;
        }

        return true;  // Consume click on popup
    }

    // Handle ratio section click
    if (m_ratioSectionRect.contains(pos)) {
        m_ratioLocked = !m_ratioLocked;
        emit lockChanged(m_ratioLocked);
        updateLayout();  // Width changes when lock state changes
        return true;
    }

    // Handle radius toggle click - toggle enabled state
    if (m_radiusToggleRect.contains(pos)) {
        m_radiusEnabled = !m_radiusEnabled;
        emit radiusEnabledChanged(m_radiusEnabled);
        if (!m_radiusEnabled) {
            // When disabling, force radius to 0
            if (m_currentRadius != 0) {
                m_currentRadius = 0;
                emit radiusChanged(0);
            }
        }
        return true;
    }

    return false;
}

bool RegionControlWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    if (!m_visible) return false;

    bool needsUpdate = false;

    // Handle slider dragging
    if (m_isDraggingSlider && pressed) {
        int newRadius = positionToRadius(pos.x());
        if (newRadius != m_currentRadius) {
            m_currentRadius = newRadius;
            emit radiusChanged(m_currentRadius);
            needsUpdate = true;
        }
    }

    // Update popup visibility based on hover
    updateSliderPopupVisibility(pos);

    // Update radius hover state
    bool radiusHovered = m_radiusToggleRect.contains(pos);
    if (radiusHovered != m_radiusHovered) {
        m_radiusHovered = radiusHovered;
        needsUpdate = true;
    }

    // Update ratio hover state
    bool ratioHovered = m_ratioSectionRect.contains(pos);
    if (ratioHovered != m_ratioHovered) {
        m_ratioHovered = ratioHovered;
        needsUpdate = true;
    }

    // Update slider button hover states
    if (m_sliderPopupVisible) {
        int newHoveredButton = -1;
        if (m_sliderMinusRect.contains(pos)) {
            newHoveredButton = 0;
        } else if (m_sliderPlusRect.contains(pos)) {
            newHoveredButton = 1;
        }

        if (newHoveredButton != m_hoveredSliderButton) {
            m_hoveredSliderButton = newHoveredButton;
            needsUpdate = true;
        }
    }

    return needsUpdate;
}

bool RegionControlWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);

    if (m_isDraggingSlider) {
        m_isDraggingSlider = false;
        return true;
    }
    return false;
}

void RegionControlWidget::updateSliderPopupVisibility(const QPoint& pos)
{
    // Only show popup when radius is enabled
    if (!m_radiusEnabled) {
        if (m_sliderPopupVisible) {
            m_sliderPopupVisible = false;
            m_hoveredSliderButton = -1;
        }
        return;
    }

    // Show popup when hovering over radius toggle
    // Keep visible when hovering over popup itself OR in the gap between
    // Create an extended area that includes toggle + gap + popup to prevent
    // popup from disappearing when moving mouse through the gap
    QRect extendedArea = m_radiusToggleRect.united(m_sliderPopupRect);

    bool shouldShow = m_radiusToggleRect.contains(pos) ||
                      (m_sliderPopupVisible && extendedArea.contains(pos));

    if (shouldShow != m_sliderPopupVisible) {
        m_sliderPopupVisible = shouldShow;
        if (!m_sliderPopupVisible) {
            m_hoveredSliderButton = -1;
        }
    }
}

// =============================================================================
// Slider Helpers
// =============================================================================

int RegionControlWidget::positionToRadius(int x) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    if (x <= trackLeft) return m_minRadius;
    if (x >= trackRight) return m_maxRadius;

    double ratio = static_cast<double>(x - trackLeft) / (trackRight - trackLeft);
    int radius = m_minRadius + static_cast<int>(ratio * (m_maxRadius - m_minRadius) + 0.5);
    return qBound(m_minRadius, radius, m_maxRadius);
}

int RegionControlWidget::radiusToPosition(int radius) const
{
    int trackLeft = m_sliderTrackRect.left();
    int trackRight = m_sliderTrackRect.right();

    double ratio = static_cast<double>(radius - m_minRadius) / (m_maxRadius - m_minRadius);
    return trackLeft + static_cast<int>(ratio * (trackRight - trackLeft));
}
